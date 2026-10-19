/*
 * prebsd -- fetch and boot an old system disk image on simh: Research Unix V4-V7 and 32V,
 * TOPS-10 6.03.
 *
 *   prebsd [options] IMAGE [command]
 *   prebsd -l                         list the images
 *   prebsd -S IMAGE                   stop an image that was started with -k
 *
 * IMAGE is a name from images.json (v7-pcollinson, tops10-603-ka, ...), a simh .ini, or a
 * single-image JSON manifest of the same shape as an images.json entry:
 *
 *     { "ini": "ini/v7-pcollinson.ini", "boot": "boot>:|hp(0,0)unix>mem =",
 *       "simh": "pdp11", "ready": "# ", "os": "unix", "retries": 1,
 *       "files": [ { "url": "https://.../rp06-0.disk.gz", "to": "v7-rp06.disk",
 *                    "sha256": "..." } ] }
 *
 * What it does:
 *   1. fetches the image's files into images/ that are missing or differ (libcurl; the download
 *      is kept in mirror/images/ and used when the upstream is gone; see mirror/README), and
 *      makes the disks the image builds itself (`install`) when `needs` is not there;
 *   2. starts the simulator (simh/bin/SIMH from ./build-simh, else $SIMH, else the PATH) on the
 *      ini, with the console on telnet;
 *   3. drives the console through `boot`, SEND>EXPECT pairs separated by '|', answering telnet
 *      negotiation.  If a step times out the simulator is started again, up to `retries`
 *      times (the PDP-10's boot sometimes hangs after the %DPA lines);
 *   4. for a Unix image sends `stty -lcase` at the shell (V7's KL11 driver uppercases);
 *      for another `os` the boot is done when `ready` shows on the console;
 *   5. runs the optional command, prints the console, and stops the simulator, or with -k
 *      leaves it running (its telnet ports stay up; -S stops it).
 *
 * Options: -k keep running, -b SEQ boot sequence, -p PORT console port, -t SECS wait per
 * step (90), -r N tries, -f fetch only, -l list.
 *
 * This is the C replacement of boot.py and fetch.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <signal.h>
#include <sys/time.h>
#include <curl/curl.h>
#include <json-c/json.h>

#define MAXFILES 32
#define CONSZ   (4u << 20)

/* telnet control bytes */
#define IAC  0xFF
#define WILL 0xFB
#define WONT 0xFC
#define DO   0xFD
#define DONT 0xFE
#define SB   0xFA
#define SE   0xF0


/* ------------------------------------------------------------------ */
/* small string helpers                                                */
/* ------------------------------------------------------------------ */

static int endswith(const char *s, const char *suf)
{
	size_t ls = strlen(s), lf = strlen(suf);
	return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

/* basename of a URL (before any query), with the final "download" path
 * segment handled the way SourceForge URLs need. */
static void url_basename(const char *url, char *out, size_t outsz)
{
	const char *base = strrchr(url, '/');
	base = base ? base + 1 : url;
	/* strip query */
	const char *q = strchr(base, '?');
	size_t n = q ? (size_t)(q - base) : strlen(base);
	if (n == 8 && strncmp(base, "download", 8) == 0) {
		/* SourceForge direct-download URL: real name is the path segment
		 * just before "download". */
		const char *p = base;
		while (p > url && p[-1] != '/')
			p--;
		if (p > url)
			p--;
		const char *s = p;
		while (s > url && s[-1] != '/')
			s--;
		q = strchr(s, '?');
		n = q ? (size_t)(q - s) : strlen(s);
		base = s;
	}
	if (n >= outsz)
		n = outsz - 1;
	memcpy(out, base, n);
	out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* sha256 + decompression, via the same helper tools the fetch script  */
/* uses (sha256sum, gunzip/unxz/bunzip2/unzip).                        */
/* ------------------------------------------------------------------ */

static int sha256_matches(const char *path, const char *want)
{
	char cmd[4096];
	/* single-quote the path, doubling any embedded single quote */
	char q[8192];
	size_t j = 0;
	q[j++] = '\'';
	for (const char *p = path; *p; p++) {
		if (*p == '\'') {
			q[j++] = '\'';
			q[j++] = '\\';
			q[j++] = '\'';
			q[j++] = '\'';
		} else
			q[j++] = *p;
	}
	q[j++] = '\'';
	q[j] = '\0';
	snprintf(cmd, sizeof cmd, "sha256sum %s", q);
	FILE *p = popen(cmd, "r");
	if (!p)
		return 0;
	char got[65];
	int ok = (fscanf(p, "%64s", got) == 1) && strcasecmp(got, want) == 0;
	pclose(p);
	return ok;
}

static int run_cmd(const char *fmt, const char *a, const char *b)
{
	char cmd[8192];
	snprintf(cmd, sizeof cmd, fmt, a, b);
	return system(cmd);
}

/* decompress `src` to `dst` based on the compressed file's basename. */
static int decompress(const char *src, const char *dst, const char *base, const char *from)
{
	if (endswith(base, ".gz"))
		return run_cmd("gunzip -c '%s' > '%s'", src, dst);
	if (endswith(base, ".xz"))
		return run_cmd("unxz -c '%s' > '%s'", src, dst);
	if (endswith(base, ".bz2"))
		return run_cmd("bunzip2 -c '%s' > '%s'", src, dst);
	if (endswith(base, ".zip")) {
		/* archive holds more than the disk (docs/pdf); unzip in place and
		 * move the target name out. */
		char dir[4096];
		snprintf(dir, sizeof dir, "%s.d", dst);
		mkdir(dir, 0755);
		if (run_cmd("unzip -o '%s' -d '%s' >/dev/null", src, dir) != 0)
			return -1;
		char inner[8192];
		snprintf(inner, sizeof inner, "%s/%s", dir, from && *from ? from : basename((char *)dst));
		if (rename(inner, dst) != 0)
			return -1;
		run_cmd("rm -rf '%s'%s", dir, "");
		return 0;
	}
	return rename(src, dst);
}

/* ------------------------------------------------------------------ */
/* libcurl download to a file                                          */
/* ------------------------------------------------------------------ */

static size_t write_file_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
	return fwrite(ptr, size, nmemb, (FILE *)ud);
}

static const char *mirror_dir(void)
{
	const char *m = getenv("PREBSD_MIRROR");
	return m && *m ? m : "mirror";
}

static int download(const char *url, const char *from, const char *to, const char *sha256,
		    const char *images_dir)
{
	char dest[4096], tmp[4096];
	snprintf(dest, sizeof dest, "%s/%s", images_dir, to);

	if (sha256 && *sha256) {
		if (access(dest, F_OK) == 0 && sha256_matches(dest, sha256)) {
			printf("  have %s (skip)\n", to);
			return 0;
		}
		if (access(dest, F_OK) == 0) {
			/* a disk that has been booted from is not the pristine one: never overwrite it */
			char moved[4200];
			snprintf(moved, sizeof moved, "%s.modified", dest);
			printf("  %s differs from the manifest: kept as %s.modified, fetching the original\n", to, to);
			if (rename(dest, moved) != 0) {
				perror("rename");
				return -1;
			}
		}
	} else if (access(dest, F_OK) == 0) {
		printf("  have %s (skip)\n", to);
		return 0;
	}

	char base[512];
	url_basename(url, base, sizeof base);
	printf("  fetch %s -> %s\n", base, to);

	/* the download as it came is kept in the mirror, and used again when it is there (its
	 * checksum below checks it like a fresh one) */
	char mdir[4096], mfile[4096];
	snprintf(mdir, sizeof mdir, "%s/images", mirror_dir());
	snprintf(mfile, sizeof mfile, "%s/%s--%s", mdir, to, base);
	{
		char mp[4096];
		snprintf(mp, sizeof mp, "%s", mirror_dir());
		mkdir(mp, 0755);
		mkdir(mdir, 0755);
	}
	snprintf(tmp, sizeof tmp, "%s/.prebsd-XXXXXX", images_dir);
	int tfd = mkstemp(tmp);
	if (tfd < 0) {
		perror("mkstemp");
		return -1;
	}
	FILE *f = fdopen(tfd, "wb");
	if (!f) {
		close(tfd);
		return -1;
	}

	if (access(mfile, R_OK) == 0) {
		char cp[8192];
		fclose(f);
		printf("  mirror %s -> %s\n", base, to);
		snprintf(cp, sizeof cp, "cp '%s' '%s'", mfile, tmp);
		if (system(cp) != 0) {
			unlink(tmp);
			return -1;
		}
	} else {
		CURL *c = curl_easy_init();
		if (!c) {
			fclose(f);
			return -1;
		}
		curl_easy_setopt(c, CURLOPT_URL, url);
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_file_cb);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
		CURLcode rc = curl_easy_perform(c);
		curl_easy_cleanup(c);
		fclose(f);
		if (rc != CURLE_OK) {
			fprintf(stderr, "  download failed: %s (and the mirror has no copy)\n", curl_easy_strerror(rc));
			unlink(tmp);
			return -1;
		}
		{
			char cp[8192];
			snprintf(cp, sizeof cp, "cp '%s' '%s.part' && mv '%s.part' '%s'", tmp, mfile, mfile, mfile);
			if (system(cp) != 0)
				fprintf(stderr, "  (could not keep a copy in the mirror)\n");
		}
	}

	if (decompress(tmp, dest, base, from) != 0) {
		fprintf(stderr, "  decompress failed for %s\n", to);
		unlink(tmp);
		return -1;
	}
	unlink(tmp);

	if (sha256 && *sha256 && !sha256_matches(dest, sha256)) {
		char bad[4200];
		fprintf(stderr, "  CHECKSUM MISMATCH: %s: kept as %s.bad\n", to, to);
		snprintf(bad, sizeof bad, "%s.bad", dest);
		rename(dest, bad);
		return -1;
	}
	printf("  ok %s\n", to);
	return 0;
}

/* ------------------------------------------------------------------ */
/* json manifest                                                       */
/* ------------------------------------------------------------------ */

struct image {
	char *name, *ini, *boot, *simh, *ready, *os, *install, *needs;
	int retries, nfiles;
	char *url[MAXFILES], *from[MAXFILES], *to[MAXFILES], *sha[MAXFILES];
};

static char *jstr(json_object *o, const char *key)
{
	json_object *v;
	if (json_object_object_get_ex(o, key, &v) && json_object_get_string(v)[0])
		return strdup(json_object_get_string(v));
	return NULL;
}

/* An image from an images.json entry (or a single-image manifest). */
static void load_image(json_object *o, struct image *im)
{
	json_object *v;
	memset(im, 0, sizeof *im);
	im->name = jstr(o, "name");
	im->ini = jstr(o, "ini");
	im->boot = jstr(o, "boot");
	im->simh = jstr(o, "simh");
	im->ready = jstr(o, "ready");
	im->os = jstr(o, "os");
	im->install = jstr(o, "install");
	im->needs = jstr(o, "needs");
	im->retries = json_object_object_get_ex(o, "retries", &v) ? json_object_get_int(v) : 1;
	if (json_object_object_get_ex(o, "files", &v) && json_object_is_type(v, json_type_array)) {
		int n = json_object_array_length(v);
		for (int i = 0; i < n && im->nfiles < MAXFILES; i++) {
			json_object *fe = json_object_array_get_idx(v, i);
			char *u = jstr(fe, "url"), *t = jstr(fe, "to");
			if (u && t) {
				im->url[im->nfiles] = u;
				im->to[im->nfiles] = t;
				im->from[im->nfiles] = jstr(fe, "from");
				im->sha[im->nfiles] = jstr(fe, "sha256");
				im->nfiles++;
			}
		}
	}
}

/* The entry of images.json named `name`, or whose ini has the basename of `ini`. */
static int find_image(const char *name, const char *ini, struct image *im)
{
	json_object *o = json_object_from_file("images.json"), *images;
	int found = 0;
	char base[256] = "";
	if (ini)
		snprintf(base, sizeof base, "%s", basename((char *)ini));
	if (!o)
		return 0;
	if (json_object_object_get_ex(o, "images", &images)
	    && json_object_is_type(images, json_type_array)) {
		int n = json_object_array_length(images);
		for (int i = 0; i < n && !found; i++) {
			json_object *e = json_object_array_get_idx(images, i), *v;
			const char *nm = json_object_object_get_ex(e, "name", &v) ? json_object_get_string(v) : "";
			const char *in = json_object_object_get_ex(e, "ini", &v) ? json_object_get_string(v) : "";
			if ((name && strcmp(nm, name) == 0) || (ini && strcmp(basename((char *)in), base) == 0)) {
				load_image(e, im);
				found = 1;
			}
		}
	}
	json_object_put(o);
	return found;
}

static void list_images(void)
{
	json_object *o = json_object_from_file("images.json"), *images;
	if (!o || !json_object_object_get_ex(o, "images", &images))
		return;
	int n = json_object_array_length(images);
	for (int i = 0; i < n; i++) {
		json_object *e = json_object_array_get_idx(images, i), *v;
		const char *nm = json_object_object_get_ex(e, "name", &v) ? json_object_get_string(v) : "";
		const char *ds = json_object_object_get_ex(e, "description", &v) ? json_object_get_string(v) : "";
		const char *bt = json_object_object_get_ex(e, "boot", &v) ? json_object_get_string(v) : "";
		printf("  %-16s %-60s %s\n", nm, ds, *bt ? bt : "(manual install)");
	}
	json_object_put(o);
}

/* ------------------------------------------------------------------ */
/* telnet console                                                      */
/* ------------------------------------------------------------------ */

static char console[CONSZ];
static size_t console_len;

static size_t negotiate(const unsigned char *buf, size_t n, unsigned char *rep,
			size_t *replen)
{
	size_t out = 0, rp = 0, i = 0;
	while (i < n) {
		unsigned char b = buf[i];
		if (b == IAC) {
			if (i + 1 >= n)
				break;
			unsigned char c = buf[i + 1];
			if (c == WILL || c == WONT || c == DO || c == DONT) {
				if (i + 2 >= n)
					break;
				unsigned char o = buf[i + 2];
				unsigned char ans = (c == WILL || c == WONT) ? DONT : WONT;
				rep[rp++] = IAC;
				rep[rp++] = ans;
				rep[rp++] = o;
				i += 3;
			} else if (c == IAC) {
				if (console_len + 1 < CONSZ)
					console[console_len++] = IAC;
				out++;
				i += 2;
			} else if (c == SB) {
				while (i + 1 < n && !(buf[i] == IAC && buf[i + 1] == SE))
					i++;
				i += 2;
			} else {
				i += 2;
			}
		} else {
			if (console_len + 1 < CONSZ)
				console[console_len++] = b;
			out++;
			i++;
		}
	}
	*replen = rp;
	return out;
}

static int tcp_connect(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/* non-blocking poll of the console, running telnet negotiation */
static void poll_console(int fd, double dur)
{
	struct timeval start;
	gettimeofday(&start, NULL);
	while (1) {
		struct timeval now, tv;
		gettimeofday(&now, NULL);
		double el = (now.tv_sec - start.tv_sec) +
			(now.tv_usec - start.tv_usec) / 1e6;
		if (el >= dur)
			break;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 50000;
		int r = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (r > 0) {
			unsigned char buf[65536];
			ssize_t n = recv(fd, buf, sizeof buf, 0);
			if (n <= 0)
				break;
			unsigned char rep[256];
			size_t replen = 0;
			negotiate(buf, (size_t)n, rep, &replen);
			if (replen)
				send(fd, rep, replen, 0);
		}
	}
}

static int has_sub(const char *sub)
{
	size_t l = strlen(sub);
	if (l == 0 || l > console_len)
		return 0;
	for (size_t i = 0; i + l <= console_len; i++)
		if (memcmp(console + i, sub, l) == 0)
			return 1;
	return 0;
}

static int wait_for(int fd, const char *sub, int timeout)
{
	time_t end = time(NULL) + timeout;
	while (time(NULL) < end && !has_sub(sub))
		poll_console(fd, 0.5);
	return has_sub(sub);
}

/* like has_sub(), but only over console bytes added after `from` — used to wait
 * for a *new* prompt (the console already holds a stale "# " from boot). */
static int has_sub_since(const char *sub, size_t from)
{
	size_t l = strlen(sub);
	if (l == 0 || l > console_len || from >= console_len)
		return 0;
	for (size_t i = from; i + l <= console_len; i++)
		if (memcmp(console + i, sub, l) == 0)
			return 1;
	return 0;
}

static int wait_for_since(int fd, const char *sub, int timeout, size_t from)
{
	time_t end = time(NULL) + timeout;
	while (time(NULL) < end && !has_sub_since(sub, from))
		poll_console(fd, 0.5);
	return has_sub_since(sub, from);
}

static void send_console(int fd, const char *s)
{
	char buf[512];
	snprintf(buf, sizeof buf, "%s\r", s);
	send(fd, buf, strlen(buf), 0);
}


/* ------------------------------------------------------------------ */
/* simh process                                                        */
/* ------------------------------------------------------------------ */

/* Start the simulator on `base` in `ini_dir`: $SIMH, else simh/bin/NAME (./build-simh), else
 * NAME on the PATH.  Its stdin is /dev/null (the console is on telnet; an open pipe there
 * stops the telnet console from answering); with `logfile` its output goes there, and it
 * leaves our process group, so that it outlives us. */
static pid_t start_simh(const char *ini_dir, const char *base, const char *dflt,
			const char *logfile)
{
	char local[4096] = "", rel[1024];
	const char *want = dflt && *dflt ? dflt : "pdp11";

	snprintf(rel, sizeof rel, "simh/bin/%s", want);
	if (!realpath(rel, local) || access(local, X_OK) != 0)
		local[0] = '\0';
	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		int devnull = open("/dev/null", O_RDONLY);
		if (logfile)
			setsid();
		if (chdir(ini_dir) != 0) {
			perror("chdir");
			_exit(127);
		}
		if (devnull >= 0) {
			dup2(devnull, 0);
			close(devnull);
		}
		if (logfile) {
			int lf = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (lf >= 0) {
				dup2(lf, 1);
				dup2(lf, 2);
				if (lf > 2)
					close(lf);
			}
		}
		const char *simh = getenv("SIMH");
		if (!simh || !*simh)
			simh = *local ? local : want;
		execlp(simh, simh, base, (char *)NULL);
		perror("execlp");
		_exit(127);
	}
	return pid;
}

static void stop_simh(pid_t pid)
{
	int st;
	kill(pid, SIGTERM);
	waitpid(pid, &st, 0);
}

/* wait until nothing listens on `port`: the next simulator cannot bind it before */
static void wait_port_free(int port)
{
	for (int i = 0; i < 120; i++) {
		int fd = tcp_connect(port);
		if (fd < 0)
			return;
		close(fd);
		usleep(250000);
	}
}

static void split_path(const char *path, char *dir, size_t dirsz,
		       char *base, size_t basesz)
{
	const char *slash = strrchr(path, '/');
	if (slash) {
		size_t n = (size_t)(slash - path);
		if (n >= dirsz)
			n = dirsz - 1;
		memcpy(dir, path, n);
		dir[n] = '\0';
		snprintf(base, basesz, "%s", slash + 1);
	} else {
		snprintf(dir, dirsz, ".");
		snprintf(base, basesz, "%s", path);
	}
}

static int parse_port(const char *ini)
{
	FILE *f = fopen(ini, "r");
	if (!f)
		return 10023;
	char line[512];
	int port = 0;
	while (fgets(line, sizeof line, f)) {
		const char *p;
		if (line[0] == ';' || line[0] == '#')
			continue;
		/* "set console telnet=10025" */
		p = strcasestr(line, "telnet=");
		if (p && strcasestr(line, "console")) {
			port = atoi(p + 7);
			break;
		}
	}
	fclose(f);
	return port ? port : 10023;
}

/* ------------------------------------------------------------------ */
/* running images: simh/run/NAME.pid holds "pid port"                  */
/* ------------------------------------------------------------------ */

static void run_file(const char *name, char *out, size_t sz)
{
	snprintf(out, sz, "simh/run/%s.pid", name);
}

static int stop_image(const char *name)
{
	char f[1024];
	long pid = 0;
	int port = 0;
	FILE *fp;

	run_file(name, f, sizeof f);
	fp = fopen(f, "r");
	if (!fp || fscanf(fp, "%ld %d", &pid, &port) < 1) {
		fprintf(stderr, "prebsd: %s is not running (no %s)\n", name, f);
		if (fp)
			fclose(fp);
		return 1;
	}
	fclose(fp);
	kill((pid_t)pid, SIGTERM);
	for (int i = 0; i < 40 && kill((pid_t)pid, 0) == 0; i++)
		usleep(250000);
	if (kill((pid_t)pid, 0) == 0)
		kill((pid_t)pid, SIGKILL);
	unlink(f);
	if (port)
		wait_port_free(port);
	printf("prebsd: %s stopped\n", name);
	return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void usage(const char *a0)
{
	fprintf(stderr,
	    "usage: %s [options] IMAGE [command]\n"
	    "       %s -l | -S IMAGE\n"
	    "IMAGE is a name in images.json, a simh .ini, or a single-image .json manifest\n"
	    "  -k       leave the simulator running after the boot (-S IMAGE stops it)\n"
	    "  -b SEQ   boot sequence SEND>EXPECT|SEND>EXPECT... (default: the image's)\n"
	    "  -p PORT  console telnet port (default: the ini's set console telnet=)\n"
	    "  -t SECS  wait for each step (90)\n"
	    "  -r N     tries when the boot hangs (the image's, else 1)\n"
	    "  -f       fetch the files only\n"
	    "  -l       list the images\n", a0, a0);
}

/* the console log is searched case-insensitively for the ready text of a non-Unix system */
static int has_sub_nocase(const char *sub)
{
	size_t l = strlen(sub);
	for (size_t i = 0; l && i + l <= console_len; i++)
		if (strncasecmp(console + i, sub, l) == 0)
			return 1;
	return 0;
}

/* One try: start the simulator, drive the boot.  Returns the pid, or -1 when the boot did not
 * get through its sequence. */
static pid_t boot_once(const char *ini_dir, const char *ini_base, const char *simh, int port,
		       const char *boot, int timeout, const char *logfile, int *fdp)
{
	pid_t pid = start_simh(ini_dir, ini_base, simh, logfile);
	int fd = -1;

	console_len = 0;
	if (pid < 0)
		return -1;
	for (int i = 0; i < 80 && fd < 0; i++) {
		fd = tcp_connect(port);
		if (fd < 0)
			usleep(250000);
	}
	if (fd < 0) {
		fprintf(stderr, "prebsd: telnet connect failed\n");
		stop_simh(pid);
		return -1;
	}
	printf("prebsd: connected to telnet console\n");
	poll_console(fd, 2.0);

	char *seq = strdup(boot), *save = NULL;
	int ok = 1;
	for (char *tok = strtok_r(seq, "|", &save); tok; tok = strtok_r(NULL, "|", &save)) {
		char *gt = strchr(tok, '>');
		if (!gt)
			continue;
		*gt = '\0';
		const char *send = tok, *expect = gt + 1;
		if (!*expect)
			continue;
		send_console(fd, send);
		printf("[driver] sent %s, waiting for %s\n", send, expect);
		if (!wait_for(fd, expect, timeout)) {
			printf("[driver] TIMEOUT waiting for %s\n", expect);
			ok = 0;
			break;
		}
	}
	free(seq);
	if (!ok) {
		printf("=== console (this try) ===\n%.*s\n===\n", (int)console_len, console);
		close(fd);
		stop_simh(pid);
		wait_port_free(port);
		return -1;
	}
	*fdp = fd;
	return pid;
}

int main(int argc, char **argv)
{
	int keep = 0, fetch_only = 0, list = 0, port = 0, timeout = 90, tries = 0, c;
	const char *bootopt = NULL, *stop = NULL;

	/* run from the directory prebsd lives in when it is not the current one */
	if (access("images.json", R_OK) != 0) {
		char self[4096];
		ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
		if (r > 0) {
			self[r] = '\0';
			if (chdir(dirname(self)) != 0)
				perror("chdir");
		}
	}
	while ((c = getopt(argc, argv, "kb:p:t:r:flS:h")) != -1) {
		switch (c) {
		case 'k': keep = 1; break;
		case 'b': bootopt = optarg; break;
		case 'p': port = atoi(optarg); break;
		case 't': timeout = atoi(optarg); break;
		case 'r': tries = atoi(optarg); break;
		case 'f': fetch_only = 1; break;
		case 'l': list = 1; break;
		case 'S': stop = optarg; break;
		default: usage(argv[0]); return c == 'h' ? 0 : 2;
		}
	}
	if (list) {
		printf("images (images.json):\n");
		list_images();
		return 0;
	}
	if (stop)
		return stop_image(stop);
	if (optind >= argc) {
		usage(argv[0]);
		return 2;
	}
	const char *target = argv[optind];
	const char *cmd = optind + 1 < argc ? argv[optind + 1] : NULL;
	struct image im;
	char *ini = NULL;

	if (endswith(target, ".json")) {
		json_object *o = json_object_from_file(target);
		if (!o) {
			fprintf(stderr, "prebsd: cannot parse %s\n", target);
			return 1;
		}
		load_image(o, &im);
		json_object_put(o);
		if (!im.ini) {
			fprintf(stderr, "prebsd: %s has no ini\n", target);
			return 1;
		}
		ini = strdup(im.ini);              /* as is: relative to the current directory */
		if (!im.name)
			im.name = strdup(basename((char *)target));
	} else if (endswith(target, ".ini")) {
		if (!find_image(NULL, target, &im)) {
			memset(&im, 0, sizeof im);
			im.retries = 1;
		}
		ini = strdup(target);
		if (!im.name)
			im.name = strdup(basename((char *)target));
	} else {
		if (!find_image(target, NULL, &im)) {
			fprintf(stderr, "prebsd: no image %s (prebsd -l lists them)\n", target);
			return 1;
		}
		char p[1024];
		snprintf(p, sizeof p, "ini/%s", im.ini);
		ini = strdup(p);
	}

	/* the files */
	mkdir("images", 0755);
	for (int i = 0; i < im.nfiles; i++)
		if (download(im.url[i], im.from[i], im.to[i], im.sha[i], "images") != 0)
			return 1;
	if (im.needs) {
		char p[1024];
		snprintf(p, sizeof p, "images/%s", im.needs);
		if (access(p, F_OK) != 0 && im.install) {
			printf("prebsd: %s is not there: running %s\n", im.needs, im.install);
			if (system(im.install) != 0 || access(p, F_OK) != 0) {
				fprintf(stderr, "prebsd: %s did not make %s\n", im.install, im.needs);
				return 1;
			}
		}
	}
	if (fetch_only)
		return 0;

	int is_unix = !im.os || strcmp(im.os, "unix") == 0;
	const char *ready_text = im.ready ? im.ready : "# ";
	const char *boot = bootopt ? bootopt : im.boot ? im.boot : "boot>:|hp(0,0)unix>mem =";
	if (!port)
		port = parse_port(ini);
	if (tries <= 0)
		tries = im.retries > 0 ? im.retries : 1;

	char ini_dir[4096], ini_base[256], logfile[1024], pidfile[1024];
	split_path(ini, ini_dir, sizeof ini_dir, ini_base, sizeof ini_base);
	mkdir("simh", 0755);
	mkdir("simh/run", 0755);
	snprintf(logfile, sizeof logfile, "%s/simh/run/%s.log", getcwd(NULL, 0), im.name);
	run_file(im.name, pidfile, sizeof pidfile);

	printf("prebsd: simh ini %s (port %d)\n", ini, port);
	printf("prebsd: boot sequence %s\n", boot);

	/* a copy of this image that is still running holds the ports */
	if (access(pidfile, F_OK) == 0) {
		fprintf(stderr, "prebsd: %s seems to be running already (prebsd -S %s stops it)\n",
		    im.name, im.name);
		return 1;
	}

	pid_t pid = -1;
	int fd = -1;
	for (int t = 1; t <= tries && pid < 0; t++) {
		if (t > 1)
			printf("prebsd: the boot hung: try %d of %d\n", t, tries);
		pid = boot_once(ini_dir, ini_base, im.simh, port, boot, timeout,
		    keep ? logfile : NULL, &fd);
	}
	if (pid < 0)
		return 1;

	int at_shell = wait_for(fd, ready_text, 60);

	/* V7's KL11 console driver hard-codes LCASE (uppercases output); clear it and wait for
	 * the shell to act on it: a fresh "# " prompt */
	if (at_shell && is_unix) {
		size_t mark = console_len;
		send_console(fd, "stty -lcase");
		if (!wait_for_since(fd, "# ", 30, mark))
			printf("[driver] TIMEOUT waiting for stty -lcase\n");
	}
	if (cmd) {
		size_t mark = console_len;
		send_console(fd, cmd);
		if (at_shell && !wait_for_since(fd, ready_text, 90, mark))
			printf("[driver] TIMEOUT waiting for command completion\n");
	}
	poll_console(fd, 1.0);

	int booted = is_unix ? (has_sub("# ") || has_sub("@")) : has_sub_nocase(ready_text);
	printf("\n=== console ===\n%.*s\n", (int)console_len, console);
	if (is_unix)
		printf("=== [driver] booted to a shell prompt: %s ===\n", booted ? "yes" : "no");
	else
		printf("=== [driver] booted (%s on the console): %s ===\n", ready_text,
		    booted ? "yes" : "no");

	if (keep && booted) {
		/* the simulator stops when its console connection is lost: a holder process keeps it
		 * open, answering telnet and appending what the operator's terminal shows to
		 * simh/run/NAME.console */
		fflush(stdout);
		pid_t holder = fork();
		if (holder == 0) {
			char cf[1100];
			unsigned char buf[65536];
			int cfd;
			int dn = open("/dev/null", O_RDWR);
			setsid();
			if (dn >= 0) {                  /* do not hold the caller's pipes open */
				dup2(dn, 0);
				dup2(dn, 1);
				dup2(dn, 2);
			}
			snprintf(cf, sizeof cf, "simh/run/%s.console", im.name);
			cfd = open(cf, O_WRONLY | O_CREAT | O_APPEND, 0644);
			for (;;) {
				ssize_t n = recv(fd, buf, sizeof buf, 0);
				unsigned char rep[256];
				size_t replen = 0;
				if (n <= 0)
					_exit(0);
				console_len = 0;
				negotiate(buf, (size_t)n, rep, &replen);
				if (replen)
					send(fd, rep, replen, 0);
				if (cfd >= 0 && console_len && write(cfd, console, console_len) < 0)
					_exit(1);
			}
		}
		close(fd);
		FILE *pf = fopen(pidfile, "w");
		if (pf) {
			fprintf(pf, "%ld %d\n", (long)pid, port);
			fclose(pf);
		}
		printf("prebsd: %s is up (simulator pid %ld, console telnet %d, log %s); prebsd -S %s stops it\n",
		    im.name, (long)pid, port, logfile, im.name);
		return 0;
	}
	close(fd);
	stop_simh(pid);
	wait_port_free(port);
	return booted ? 0 : 1;
}

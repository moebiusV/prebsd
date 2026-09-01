/*
 * prebsd — fetch and boot a Research Unix disk image on simh.
 *
 * Usage: prebsd <file>
 *
 * <file> is either:
 *   - a simh .ini (device/CPU/boot config); the boot sequence is looked up
 *     from images.json by the ini's basename, or defaults to the V7 RP06 one.
 *   - a single-image JSON manifest that ties a disk image to its simh config:
 *
 *         {
 *           "ini":  "ini/v7-pcollinson.ini",
 *           "boot": "boot>:|hp(0,0)unix>mem =",
 *           "files": [ { "url": "https://.../rp06-0.disk.gz",
 *                        "to":  "v7-rp06.disk",
 *                        "sha256": "..." } ]
 *         }
 *
 * Any file whose sha256 does not match (or that is absent) is fetched with
 * libcurl and decompressed into images/.  Then simh is started on the ini and
 * its telnet console is driven through the boot sequence, answering telnet IAC
 * negotiation, and `stty -lcase` is sent once the shell is reached (V7's KL11
 * driver hard-codes LCASE, uppercasing output).
 *
 * This is the C rewrite of boot.py + fetch.
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

static const char *default_boot = "boot>:|hp(0,0)unix>mem =";

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
static int decompress(const char *src, const char *dst, const char *base)
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
		snprintf(inner, sizeof inner, "%s/%s", dir, basename((char *)dst));
		if (rename(inner, dst) != 0)
			return -1;
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

static int download(const char *url, const char *to, const char *sha256,
		    const char *images_dir)
{
	char dest[4096], tmp[4096];
	snprintf(dest, sizeof dest, "%s/%s", images_dir, to);

	if (sha256 && *sha256) {
		if (access(dest, F_OK) == 0 && sha256_matches(dest, sha256)) {
			printf("  have %s (skip)\n", to);
			return 0;
		}
	} else if (access(dest, F_OK) == 0) {
		printf("  have %s (skip)\n", to);
		return 0;
	}

	char base[512];
	url_basename(url, base, sizeof base);
	printf("  fetch %s -> %s\n", base, to);

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

	CURL *c = curl_easy_init();
	if (!c) {
		fclose(f);
		return -1;
	}
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_file_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_cleanup(c);
	fclose(f);
	if (rc != CURLE_OK) {
		fprintf(stderr, "  download failed: %s\n", curl_easy_strerror(rc));
		unlink(tmp);
		return -1;
	}

	if (decompress(tmp, dest, base) != 0) {
		fprintf(stderr, "  decompress failed for %s\n", to);
		unlink(tmp);
		return -1;
	}
	unlink(tmp);

	if (sha256 && *sha256 && !sha256_matches(dest, sha256)) {
		fprintf(stderr, "  CHECKSUM MISMATCH: %s\n", to);
		return -1;
	}
	printf("  ok %s\n", to);
	return 0;
}

/* ------------------------------------------------------------------ */
/* json manifest                                                       */
/* ------------------------------------------------------------------ */

/* Parse a single-image manifest { ini, boot, files:[{url,to,sha256}] }.
 * Returns 0 on success; *ini, *boot are strdup'd (caller frees), and the
 * files array is filled. */
static int parse_image_json(const char *path, char **ini, char **boot,
			    const char **url, const char **to, const char **sha,
			    int *nfiles)
{
	json_object *o = json_object_from_file(path);
	if (!o) {
		fprintf(stderr, "cannot parse %s\n", path);
		return -1;
	}
	*ini = NULL;
	*boot = NULL;

	json_object *v;
	if (json_object_object_get_ex(o, "ini", &v))
		*ini = strdup(json_object_get_string(v));
	if (json_object_object_get_ex(o, "boot", &v))
		*boot = strdup(json_object_get_string(v));

	*nfiles = 0;
	if (json_object_object_get_ex(o, "files", &v) &&
	    json_object_is_type(v, json_type_array)) {
		int n = json_object_array_length(v);
		for (int i = 0; i < n && *nfiles < MAXFILES; i++) {
			json_object *fe = json_object_array_get_idx(v, i);
			json_object *u, *t, *s;
			url[*nfiles] = json_object_object_get_ex(fe, "url", &u) ?
				strdup(json_object_get_string(u)) : NULL;
			to[*nfiles] = json_object_object_get_ex(fe, "to", &t) ?
				strdup(json_object_get_string(t)) : NULL;
			sha[*nfiles] = json_object_object_get_ex(fe, "sha256", &s) ?
				strdup(json_object_get_string(s)) : NULL;
			if (url[*nfiles] && to[*nfiles])
				(*nfiles)++;
		}
	}
	json_object_put(o);
	return (*ini && *nfiles >= 0) ? 0 : -1;
}

/* Look up a boot sequence from images.json by the ini's basename. */
static char *lookup_boot(const char *ini)
{
	const char *manifest = "images.json";
	json_object *o = json_object_from_file(manifest);
	if (!o)
		return NULL;
	char *result = NULL;
	char base[256];
	snprintf(base, sizeof base, "%s", basename((char *)ini));
	json_object *images;
	if (json_object_object_get_ex(o, "images", &images) &&
	    json_object_is_type(images, json_type_array)) {
		int n = json_object_array_length(images);
		for (int i = 0; i < n; i++) {
			json_object *img = json_object_array_get_idx(images, i);
			json_object *iv, *bv;
			const char *name = json_object_object_get_ex(img, "ini", &iv) ?
				json_object_get_string(iv) : "";
			if (strcmp(basename((char *)name), base) == 0 &&
			    json_object_object_get_ex(img, "boot", &bv) &&
			    json_object_get_string(bv)[0]) {
				result = strdup(json_object_get_string(bv));
				break;
			}
		}
	}
	json_object_put(o);
	return result;
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

static void send_console(int fd, const char *s)
{
	char buf[512];
	snprintf(buf, sizeof buf, "%s\r", s);
	send(fd, buf, strlen(buf), 0);
}

/* ------------------------------------------------------------------ */
/* simh process                                                        */
/* ------------------------------------------------------------------ */

static pid_t start_simh(const char *ini_dir, const char *base)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		if (chdir(ini_dir) != 0) {
			perror("chdir");
			_exit(127);
		}
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull >= 0) {
			dup2(devnull, 0);
			close(devnull);
		}
		const char *simh = getenv("SIMH");
		if (!simh || !*simh)
			simh = "pdp11";
		execlp(simh, simh, base, (char *)NULL);
		perror("execlp");
		_exit(127);
	}
	return pid;
}

/* split a path into dir + base without dirname()/basename(), which (POSIX
 * dirname) may modify their argument. */
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
		/* "set console telnet=10025" */
		const char *p = strcasestr(line, "telnet=");
		if (p) {
			port = atoi(p + 7);
			break;
		}
	}
	fclose(f);
	return port ? port : 10023;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <file.ini|file.json>\n", argv[0]);
		return 2;
	}

	char self[4096];
	ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
	if (r < 0)
		strcpy(self, argv[0]);
	else
		self[r] = '\0';
	char root[4096];
	snprintf(root, sizeof root, "%s", dirname(self));
	char images_dir[4096];
	snprintf(images_dir, sizeof images_dir, "%s/images", root);

	char *ini = NULL;
	char *boot = NULL;
	const char *url[MAXFILES], *to[MAXFILES], *sha[MAXFILES];
	int nfiles = 0;

	if (endswith(argv[1], ".json")) {
		if (parse_image_json(argv[1], &ini, &boot, url, to, sha, &nfiles)
		    != 0) {
			fprintf(stderr, "bad image json: %s\n", argv[1]);
			return 1;
		}
		/* json `ini` is relative to the json's directory */
		char jdir[4096], jbase[256], full[4096];
		split_path(argv[1], jdir, sizeof jdir, jbase, sizeof jbase);
		snprintf(full, sizeof full, "%s/%s", jdir, ini);
		free(ini);
		ini = strdup(full);

		mkdir(images_dir, 0755);
		for (int i = 0; i < nfiles; i++) {
			if (download(url[i], to[i], sha[i], images_dir) != 0)
				return 1;
		}
	} else {
		ini = strdup(argv[1]);
		boot = lookup_boot(ini);
	}

	if (!boot || !*boot) {
		free(boot);
		boot = strdup(default_boot);
	}

	int port = parse_port(ini);

	char ini_dir[4096], ini_base[256];
	split_path(ini, ini_dir, sizeof ini_dir, ini_base, sizeof ini_base);

	printf("prebsd: simh ini %s (port %d)\n", ini, port);
	printf("prebsd: boot sequence %s\n", boot);

	pid_t pid = start_simh(ini_dir, ini_base);
	if (pid < 0)
		return 1;

	/* connect to the telnet console */
	int fd = -1;
	for (int i = 0; i < 80 && fd < 0; i++) {
		fd = tcp_connect(port);
		if (fd < 0)
			usleep(250000);
	}
	if (fd < 0) {
		fprintf(stderr, "prebsd: telnet connect failed\n");
		kill(pid, SIGKILL);
		return 1;
	}
	printf("prebsd: connected to telnet console\n");

	poll_console(fd, 2.0);

	/* drive the boot sequence: SEND>EXPECT pairs joined by '|' */
	char *seq = strdup(boot);
	char *save = NULL;
	for (char *tok = strtok_r(seq, "|", &save); tok;
	     tok = strtok_r(NULL, "|", &save)) {
		char *gt = strchr(tok, '>');
		if (!gt)
			continue;
		*gt = '\0';
		const char *send = tok, *expect = gt + 1;
		if (!*expect)
			continue;
		send_console(fd, send);
		printf("[driver] sent %s, waiting for %s\n", send, expect);
		if (!wait_for(fd, expect, 90)) {
			printf("[driver] TIMEOUT waiting for %s\n", expect);
			break;
		}
	}
	free(seq);

	/* V7's KL11 console driver hard-codes LCASE (uppercases output); clear it */
	send_console(fd, "stty -lcase");
	poll_console(fd, 0.5);

	/* settle, then tear down and report */
	poll_console(fd, 1.0);
	close(fd);

	kill(pid, SIGTERM);
	int st;
	waitpid(pid, &st, 0);

	/* report the boot result */
	printf("\n=== console ===\n%.*s\n", (int)console_len, console);
	for (size_t i = 0; i < console_len; i++)
		console[i] = toupper((unsigned char)console[i]);
	int booted = has_sub("# ") || has_sub("@");
	printf("=== [driver] booted to a shell prompt: %s ===\n",
	       booted ? "yes" : "no");

	free(ini);
	free(boot);
	return booted ? 0 : 1;
}

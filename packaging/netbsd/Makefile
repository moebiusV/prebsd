DISTNAME=	prebsd-0.1
CATEGORIES=	emulators
MASTER_SITES=	https://github.com/moebiusV/prebsd/releases/download/v0.1/
EXTRACT_SUFX=	.tar.gz

MAINTAINER=	maintainer@example.com
HOMEPAGE=	https://github.com/moebiusV/prebsd
COMMENT=	Fetch and run Research Unix V4-V7 and 32V on SIMH
LICENSE=	isc

NO_BUILD=	yes
GNU_CONFIGURE=	yes
USE_TOOLS+=	gmake

# recommended (optional): ../../sysutils/filsys, ../../devel/v7unix-toolchain, ../../emulators/simh
.include "../../mk/bsd.pkg.mk"

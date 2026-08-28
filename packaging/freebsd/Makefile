PORTNAME=	prebsd
PORTVERSION=	0.1
CATEGORIES=	emulators
MASTER_SITES=	https://github.com/moebiusV/prebsd/releases/download/v${PORTVERSION}/

MAINTAINER=	maintainer@example.com
COMMENT=	Fetch and run Research Unix V4-V7 and 32V on SIMH
WWW=		https://github.com/moebiusV/prebsd

LICENSE=	ISCL

NO_BUILD=	yes
NO_ARCH=	yes
USES=		gmake
GNU_CONFIGURE=	yes

RUN_DEPENDS=	curl:ftp/curl

.include <bsd.port.mk>

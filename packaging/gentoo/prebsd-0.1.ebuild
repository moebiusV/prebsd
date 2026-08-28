# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="Fetch and run Research Unix V4-V7 and 32V on SIMH"
HOMEPAGE="https://github.com/moebiusV/prebsd"
SRC_URI="https://github.com/moebiusV/prebsd/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"
LICENSE="ISC"
SLOT="0"
KEYWORDS="~amd64"

RDEPEND="net-misc/curl"

src_configure() {
	econf
}

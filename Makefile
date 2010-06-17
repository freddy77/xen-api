# Set this to some other value on systems with no Xen libs to
# build only the client stuff and the fakeserver.
COMPILE_XENSTUFF ?= yes
export COMPILE_XENSTUFF

PRODUCT_VERSION?=unknown
PRODUCT_BRAND?=Miamidev
BUILD_NUMBER?=6666
COMPILE_NATIVE?=yes
export PRODUCT_VERSION PRODUCT_BRAND BUILD_NUMBER

.PHONY: all
all:
	omake phase1 phase2
	omake lib-uninstall
	omake lib-install
	omake phase3

.PHONY: phase3
phase3:
	omake phase3

.PHONY: api
api: all
	@ :

.PHONY: xapimon
xapimon:
	omake ocaml/xapimon/xapimon

.PHONY: stresstest
stresstest:
	omake ocaml/xapi/stresstest

.PHONY: cvm
cvm:
	omake ocaml/cvm/cvm

.PHONY: install
install:
	omake install

.PHONY: lib-install
lib-install:
	omake DESTDIR=$(DESTDIR) lib-install

.PHONY: lib-uninstall
lib-uninstall:
	omake DESTDIR=$(DESTDIR) lib-uninstall

.PHONY: sdk-install
sdk-install:
	omake sdk-install

.PHONY:patch
patch:
	omake patch

.PHONY: clean
clean:
	omake clean
	omake lib-uninstall
	rm -rf dist/staging
	rm -f .omakedb .omakedb.lock

.PHONY: otags
otags:
	otags -vi -r . -o tags

.PHONY: doc
# For the doc target, we need to have phase2 done in case autogenerated .ml files are required
doc:
	omake ocaml/util/version.ml
	omake phase1 phase2
	omake doc
 
.PHONY: version
version:
	echo "let hg_id = \"$(shell hg id | sed -r 's/(.+)\s.*/\1/g')\"" > ocaml/util/version.ml
	echo "let hostname = \"$(shell hostname)\"" >> ocaml/util/version.ml
	echo "let date = \"$(shell date -u +%Y-%m-%d)\"" >> ocaml/util/version.ml
	echo "let product_version = \"$(PRODUCT_VERSION)\"" >> ocaml/util/version.ml
	echo "let product_brand = \"$(PRODUCT_BRAND)\"" >> ocaml/util/version.ml
	echo "let build_number = \"$(BUILD_NUMBER)\"" >> ocaml/util/version.ml
 
 .PHONY: clean
 clean:


ifdef B_BASE
include $(B_BASE)/common.mk
include $(B_BASE)/rpmbuild.mk
REPO=$(call hg_loc,xen-api)
else
MY_OUTPUT_DIR ?= $(CURDIR)/output
MY_OBJ_DIR ?= $(CURDIR)/obj
REPO ?= $(CURDIR)

RPM_SPECSDIR?=/usr/src/redhat/SPECS
RPM_SRPMSDIR?=/usr/src/redhat/SRPMS
RPM_SOURCESDIR?=/usr/src/redhat/SOURCES
RPMBUILD?=rpmbuild
XEN_RELEASE?=unknown
endif

.PHONY: srpm
srpm: 
	mkdir -p $(RPM_SOURCESDIR) $(RPM_SPECSDIR) $(RPM_SRPMSDIR)
	hg archive -t tbz2 $(RPM_SOURCESDIR)/xapi-0.2.tar.bz2
	make -C $(REPO) version
	rm -f $(RPM_SOURCESDIR)/xapi-version.patch
	(cd $(REPO); diff -u /dev/null ocaml/util/version.ml > $(RPM_SOURCESDIR)/xapi-version.patch) || true
	cp -f xapi.spec $(RPM_SPECSDIR)/
	chown root.root $(RPM_SPECSDIR)/xapi.spec
	$(RPMBUILD) -bs --nodeps $(RPM_SPECSDIR)/xapi.spec


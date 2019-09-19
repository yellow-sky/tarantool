GITLAB_MAKE:=${MAKE} -f .gitlab.mk
TRAVIS_MAKE:=${MAKE} -f .travis.mk

# #####
# Utils
# #####

# Update submodules.
#
# Note: There is no --force option for `git submodule` on git
# 1.7.1, which is shiped in CentOS 6.
git_submodule_update:
	git submodule update --force --recursive --init 2>/dev/null || \
		git submodule update --recursive --init

# Pass *_no_deps goals to .travis.mk.
test_%_no_deps: git_submodule_update
	${TRAVIS_MAKE} $@

# #######################################################
# Build and push testing docker images to GitLab Registry
# #######################################################

# These images contains tarantool dependencies and testing
# dependencies to run tests in them.
#
# How to run:
#
# make GITLAB_USER=foo -f .gitlab.mk docker_bootstrap
#
# The command will prompt for a password. If two-factor
# authentication is enabled an access token with 'api' scope
# should be entered here instead of a password.
#
# When to run:
#
# When some of deps_* goals in .travis.mk are updated.
#
# Keep in a mind that the resulting image is used to run tests on
# all branches, so avoid removing packages: only add them.

GITLAB_REGISTRY?=registry.gitlab.com
DOCKER_BUILD=docker build --network=host -f - .

define DEBIAN_STRETCH_DOCKERFILE
FROM packpack/packpack:debian-stretch
COPY .travis.mk .
RUN make -f .travis.mk deps_debian
endef
export DEBIAN_STRETCH_DOCKERFILE

define DEBIAN_BUSTER_DOCKERFILE
FROM packpack/packpack:debian-buster
COPY .travis.mk .
RUN make APT_EXTRA_FLAGS="--allow-releaseinfo-change-version --allow-releaseinfo-change-suite" -f .travis.mk deps_buster_clang_8
endef
export DEBIAN_BUSTER_DOCKERFILE

IMAGE_PREFIX:=${GITLAB_REGISTRY}/tarantool/tarantool/testing
DEBIAN_STRETCH_IMAGE:=${IMAGE_PREFIX}/debian-stretch
DEBIAN_BUSTER_IMAGE:=${IMAGE_PREFIX}/debian-buster

TRAVIS_CI_MD5SUM:=$(firstword $(shell md5sum .travis.mk))

docker_bootstrap:
	# Login.
	docker login -u ${GITLAB_USER} ${GITLAB_REGISTRY}
	# Build images.
	echo "$${DEBIAN_STRETCH_DOCKERFILE}" | ${DOCKER_BUILD} \
		-t ${DEBIAN_STRETCH_IMAGE}:${TRAVIS_CI_MD5SUM} \
		-t ${DEBIAN_STRETCH_IMAGE}:latest
	echo "$${DEBIAN_BUSTER_DOCKERFILE}" | ${DOCKER_BUILD} \
		-t ${DEBIAN_BUSTER_IMAGE}:${TRAVIS_CI_MD5SUM} \
		-t ${DEBIAN_BUSTER_IMAGE}:latest
	# Push images.
	docker push ${DEBIAN_STRETCH_IMAGE}:${TRAVIS_CI_MD5SUM}
	docker push ${DEBIAN_BUSTER_IMAGE}:${TRAVIS_CI_MD5SUM}
	docker push ${DEBIAN_STRETCH_IMAGE}:latest
	docker push ${DEBIAN_BUSTER_IMAGE}:latest

# #################################
# Run tests under a virtual machine
# #################################

vms_start:
	VBoxManage controlvm ${VMS_NAME} poweroff || true
	VBoxManage snapshot ${VMS_NAME} restore ${VMS_NAME}
	VBoxManage startvm ${VMS_NAME} --type headless

vms_test_%:
	tar czf - ../tarantool | ssh ${VMS_USER}@127.0.0.1 -p ${VMS_PORT} tar xzf -
	ssh ${VMS_USER}@127.0.0.1 -p ${VMS_PORT} "/bin/bash -c \
		'${EXTRA_ENV} \
		cd tarantool && \
		${GITLAB_MAKE} git_submodule_update && \
		${TRAVIS_MAKE} $(subst vms_,,$@)'"

vms_shutdown:
	VBoxManage controlvm ${VMS_NAME} poweroff

# ########################
# Build RPM / Deb packages
# ########################

# ###########################
# Sources tarballs & packages
# ###########################

# Push alpha and beta versions to <major>x bucket (say, 2x),
# stable to <major>.<minor> bucket (say, 2.2).
GIT_DESCRIBE=$(shell git describe HEAD)
MAJOR_VERSION=$(word 1,$(subst ., ,$(GIT_DESCRIBE)))
MINOR_VERSION=$(word 2,$(subst ., ,$(GIT_DESCRIBE)))
BUCKET=$(MAJOR_VERSION)_$(MINOR_VERSION)
ifeq ($(MINOR_VERSION),0)
BUCKET=$(MAJOR_VERSION)x
endif
ifeq ($(MINOR_VERSION),1)
BUCKET=$(MAJOR_VERSION)x
endif
REPOBASE=tarantool_repo/${BUCKET}/${OS}
REPOPATH=${REPOBASE}/pool/${DIST}/main/t/tarantool

# prepare the packpack repository sources
packpack_setup:
	git clone https://github.com/packpack/packpack.git packpack

# create the binaries packages at the ./build/ local path
# deploy the packages at the AWS S3 bucket path:
# tarantool_repo/<major version>[x;_<minor version>]
.PHONY: package
package: git_submodule_update packpack_setup
	PACKPACK_EXTRA_DOCKER_RUN_PARAMS='--network=host' ./packpack/packpack
	mkdir -p ${REPOPATH}
	for packfile in `ls build/*.rpm build/*.deb build/*.dsc build/*.tar.*z 2>/dev/null` ; \
		do cp $$packfile ${REPOPATH}/. ; done
	mkdir ${REPOBASE}/conf
	cp distributions ${REPOBASE}/conf
	cd ${REPOBASE} && BUCKET=${BUCKET} OS=${OS} DISTS=${DIST} ../../../pub_packs_s3.sh .

# ############
# Static build
# ############

static_build:
	docker build --network=host -f Dockerfile.staticbuild .

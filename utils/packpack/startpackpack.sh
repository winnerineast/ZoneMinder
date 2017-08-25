#!/bin/bash
# packpack setup file for the ZoneMinder project
# Written by Andrew Bauer

###############
# SUBROUTINES #
###############

# General sanity checks
checksanity () {
    # Check to see if this script has access to all the commands it needs
    for CMD in set echo curl git ln mkdir rmdir cat patch; do
      type $CMD 2>&1 > /dev/null

      if [ $? -ne 0 ]; then
        echo
        echo "ERROR: The script cannot find the required command \"${CMD}\"."
        echo
        exit 1
      fi
    done

    if [ "${OS}" == "el" ] && [ "${DIST}" == "6" ]; then
        type repoquery 2>&1 > /dev/null
    
        if [ $? -ne 0 ]; then
            echo
            echo "ERROR: The script cannot find the required command \"reqoquery\"."
            echo "This command is required in order to build ZoneMinder on el6."
            echo "Please install the \"yum-utils\" package then try again."
            echo
            exit 1
        fi
    fi
    
    # Verify OS & DIST environment variables have been set before calling this script
    if [ -z "${OS}" ] || [ -z "${DIST}" ]; then
        echo "ERROR: both OS and DIST environment variables must be set"
        exit 1
    fi

    if [ -z "${ARCH}" ]; then
        ARCH="x86_64"
    fi

    if [[ "${ARCH}" != "x86_64" && "${ARCH}" != "i386" && "${ARCH}" != "armhf" ]]; then
        echo
        echo "ERROR: Unsupported architecture specified \"${ARCH}\"."
        echo
        exit 1
    fi
}

# Create key variables used to assemble the package name
createvars () {
    # We need today's date in year/month/day format
    thedate=$(date +%Y%m%d)

    # We need the (short) commit hash of the latest commit (rpm packaging only)
    shorthash=$(git describe --long --always | awk -F - '{print $3}')

    # Grab the ZoneMinder version from the contents of the version file
    versionfile=$(cat version)

    # git the latest (short) commit hash of the version file
    versionhash=$(git log -n1 --pretty=format:%h version)

    # Number of commits since the version file was last changed
    numcommits=$(git rev-list ${versionhash}..HEAD --count)
}

# Check key variables before calling packpack
checkvars () {

    for var in $thedate $shorthash $versionfile $versionhash $numcommits; do
        if [ -z ${var} ]; then
            echo
            echo "FATAL: This script was unable to determine one or more key variables. Cannot continue."
            echo
            echo "VARIABLE DUMP"
            echo "-------------"
            echo
            echo "thedate: ${thedate}"
            echo "shorthash: ${shorthash}"
            echo "versionfile: ${versionfile}"
            echo "versionhash: ${versionhash}"
            echo "numcommits: ${numcommits}"
            echo
            exit 98
        fi
    done
}

# Steps common to all builds
commonprep () {
    mkdir -p build
    if [ -e "packpack/Makefile" ]; then
        echo "Checking packpack github repo for changes..."
        git -C packpack pull origin master
    else
        echo "Cloning packpack github repo..."
        git clone https://github.com/packpack/packpack.git packpack
    fi

    # Patch packpack
    patch --dry-run --silent -f -p1 < utils/packpack/packpack-rpm.patch
    if [ $? -eq 0 ]; then
        patch -p1 < utils/packpack/packpack-rpm.patch
    fi

    # The rpm specfile requires we download the tarball and manually move it into place
    # Might as well do this for Debian as well, rather than git submodule init
    CRUDVER="3.0.10"
    if [ -e "build/crud-${CRUDVER}.tar.gz" ]; then
        echo "Found existing Crud ${CRUDVER} tarball..."
    else
        echo "Retrieving Crud ${CRUDVER} submodule..."
        curl -L https://github.com/FriendsOfCake/crud/archive/v${CRUDVER}.tar.gz > build/crud-${CRUDVER}.tar.gz
        if [ $? -ne 0 ]; then
            echo "ERROR: Crud tarball retreival failed..."
            exit 1
        fi
    fi
}

# Uncompress the Crud tarball and move it into place
movecrud () {
    if [ -e "web/api/app/Plugin/Crud/LICENSE.txt" ]; then
        echo "Crud plugin already installed..."
    else     
        echo "Unpacking Crud plugin..."
        tar -xzf build/crud-${CRUDVER}.tar.gz
        rmdir web/api/app/Plugin/Crud
        mv -f crud-${CRUDVER} web/api/app/Plugin/Crud
    fi
}

# previsouly part of installzm.sh
# install the trusty deb and test zoneminder
installtrusty () {

    # Check we've got gdebi installed
    type gdebi 2>&1 > /dev/null

    if [ $? -ne 0 ]; then
      echo
      echo "ERROR: The script cannot find the required command \"gdebi\"."
      echo
      exit 1
    fi

    # Install and test the zoneminder package (only) for Ubuntu Trusty
    pkgname="build/zoneminder_${VERSION}-${RELEASE}_amd64.deb"

    if [ -e $pkgname ]; then
        sudo gdebi --quiet --non-interactive $pkgname
        mysql -uzmuser -pzmpass zm < db/test.monitor.sql
        sudo /usr/bin/zmpkg.pl start
        sudo /usr/bin/zmfilter.pl -f purgewhenfull
    else
      echo
      echo "ERROR: The script cannot find the package $pkgname"
      echo "Check the Travis log for a build failure."
      echo
      exit 99
    fi
}

# This sets the naming convention for the rpm packages
setrpmpkgname () {

    createvars

    # Set VERSION to the contents of the version file e.g. 1.31.0
    # Set RELEASE to 1.{number of commits}.{today's date}git{short hash of HEAD} e.g. 1.82.20170605gitg7ae0b4a
    export VERSION="$versionfile"
    export RELEASE="1.${numcommits}.${thedate}git${shorthash}"

    checkvars
    
    echo
    echo "Packpack VERSION has been set to: ${VERSION}"
    echo "Packpack RELEASE has been set to: ${RELEASE}"
    echo

}

# This sets the naming convention for the deb packages
setdebpkgname () {

    createvars

    # Set VERSION to {zm version}~{today's date}.{number of commits} e.g. 1.31.0~20170605.82
    # Set RELEASE to the packpack DIST variable e.g. Trusty
    export VERSION="${versionfile}~${thedate}.${numcommits}"
    export RELEASE="${DIST}"

    checkvars
    
    echo
    echo "Packpack VERSION has been set to: ${VERSION}"
    echo "Packpack RELEASE has been set to: ${RELEASE}"
    echo

}

# This adds an entry to the rpm specfile changelog
setrpmchangelog () {

    export CHANGELOG_NAME="Andrew Bauer"
    export CHANGELOG_EMAIL="zonexpertconsulting@outlook.com"
    export CHANGELOG_TEXT="Automated, development snapshot of git ${shorthash}"

}


# This adds an entry to the debian changelog
setdebchangelog () {
DATE=`date -R`
cat <<EOF > debian/changelog
zoneminder ($VERSION-${DIST}-1) unstable; urgency=low
  * 
 -- Isaac Connor <iconnor@connortechnology.com>  $DATE
EOF
}

# start packpack, filter the output if we are running in travis
execpackpack () {

    if [ "${OS}" == "el" ] || [ "${OS}" == "fedora" ]; then
        parms="-f utils/packpack/redhat_package.mk redhat_package"
    else
        parms=""
    fi

    if [ "${TRAVIS}" == "true"  ]; then
        utils/packpack/heartbeat.sh &
        mypid=$!
        packpack/packpack $parms > buildlog.txt 2>&1
        kill $mypid
        tail -n 3000 buildlog.txt | grep -v ONVIF
    else
        packpack/packpack $parms
    fi
}

################
# MAIN PROGRAM #
################

checksanity

# We don't want to build packages for all supported distros after every commit
# Only build all packages when executed via cron
# See https://docs.travis-ci.com/user/cron-jobs/
if [ "${TRAVIS_EVENT_TYPE}" == "cron" ] || [ "${TRAVIS}" != "true"  ]; then
    commonprep

    # Steps common to Redhat distros
    if [ "${OS}" == "el" ] || [ "${OS}" == "fedora" ]; then
        echo "Begin Redhat build..."

        setrpmpkgname

        ln -sfT distros/redhat rpm

        # The rpm specfile requires the Crud submodule folder to be empty
        rm -rf web/api/app/Plugin/Crud
        mkdir web/api/app/Plugin/Crud

        # We use zmrepo to build el6 only. All other redhat distros use rpm fusion
        if [ "${OS}" == "el" ] && [ "${DIST}" == "6" ]; then
            baseurl="https://zmrepo.zoneminder.com/el/${DIST}/x86_64/"
            reporpm="zmrepo"
            # Let repoquery determine the full url and filename to the latest zmrepo package
            dlurl=`repoquery --archlist=noarch --repofrompath=zmpackpack,${baseurl} --repoid=zmpackpack --qf="%{location}" ${reporpm} 2> /dev/null`
        else
            reporpm="rpmfusion-free-release"
            dlurl="https://download1.rpmfusion.org/free/${OS}/${reporpm}-${DIST}.noarch.rpm"
        fi

        # Give our downloaded repo rpm a common name so redhat_package.mk can find it
        if [ -n "$dlurl" ] && [ $? -eq 0  ]; then
            echo "Retrieving ${reporpm} repo rpm..."gd
            curl $dlurl > build/external-repo.noarch.rpm
        else
            echo "ERROR: Failed to retrieve ${reporpm} repo rpm..."
            echo "Download url was: $dlurl"
            exit 1
        fi

        setrpmchangelog

        echo "Starting packpack..."
        execpackpack

    # Steps common to Debian based distros
    elif [ "${OS}" == "debian" ] || [ "${OS}" == "ubuntu" ]; then
        echo "Begin ${OS} ${DIST} build..."

        setdebpkgname
        movecrud

        if [ "${DIST}" == "trusty" ] || [ "${DIST}" == "precise" ]; then
            ln -sfT distros/ubuntu1204 debian
        elif [ "${DIST}" == "wheezy" ]; then 
            ln -sfT distros/debian debian
        else 
            ln -sfT distros/ubuntu1604 debian
        fi
        
        setdebchangelog
        
        echo "Starting packpack..."
        execpackpack
        
        if [ "${OS}" == "ubuntu" ] && [ "${DIST}" == "trusty" ] && [ "${ARCH}" == "x86_64" ] && [ "${TRAVIS}" == "true" ]; then
            installtrusty
        fi
    fi

# We were not triggered via cron so just build and test trusty
elif [ "${OS}" == "ubuntu" ] && [ "${DIST}" == "trusty" ] && [ "${ARCH}" == "x86_64" ]; then
    echo "Begin Ubuntu Trusty build..."

    commonprep
    setdebpkgname
    movecrud

    ln -sfT distros/ubuntu1204 debian

    setdebchangelog
    
    echo "Starting packpack..."
    execpackpack

    # If we are running inside Travis then attempt to install the deb we just built
    if [ "${TRAVIS}" == "true" ]; then
        installtrusty
    fi
fi

exit 0


#!/bin/bash
set -xe

# manual configuration
if [ "$BUCKET" == "" ]; then
    echo "ERROR: need to set BUCKET env variable to any like <1_10,2_2,2_3,2x>"
    exit 1
fi
branch=$BUCKET

# configuration
if [ "$OS" == "" ]; then
    echo "ERROR: need to set OS env variable to any of <centos,fedora>"
    exit 1
fi
os=$OS
if [ "$RELEASE" == "" ]; then
    echo "ERROR: need to set RELEASE env variable to any set of <[6,7,27-30]>"
    exit 1
fi
release=$RELEASE

aws='aws --endpoint-url https://hb.bizmrg.com'
s3="s3://tarantool_repo/$branch"

# get packages from pointed location either default Docker's mirror
repo=$1
if [ "$repo" == "" ] ; then
    repo=.
fi
if ! ls $repo/*.rpm >/dev/null 2>&1 ; then
    echo "Error: Current '$repo' has:"
    ls -al $repo
    echo "Usage: $0 [path with *.rpm files]"
    exit 1
fi

# temporary lock the publication to the repository
ws=/tmp/tarantool_repo_s3_${branch}_${os}_${release}
wslock=$ws.lock
lockfile -l 1000 $wslock

# create temporary workspace with packages copies
rm -rf $ws
mkdir -p $ws
cp $repo/*.rpm $ws/.
cd $ws

# set the paths
if [ "$os" == "centos" -o "$os" == "el" ]; then
    repopath=centos/$release/os/x86_64
    rpmpath=Packages
elif [ "$os" == "fedora" ]; then
    repopath=fedora/releases/$release/Everything/x86_64/os
    rpmpath=Packages/t
fi
packpath=$repopath/$rpmpath

# prepare local repository with packages
mkdir -p $packpath
mv *.rpm $packpath/.
cd $repopath

# copy the current metadata files from S3
mkdir repodata.base
for file in `$aws s3 ls $s3/$repopath/repodata/ | awk '{print $NF}'` ; do
    $aws s3 ls $s3/$repopath/repodata/$file || continue
    $aws s3 cp $s3/$repopath/repodata/$file repodata.base/$file
done

# create the new repository metadata files
createrepo --no-database --update --workers=2 --compress-type=gz --simple-md-filenames .
mv repodata repodata.adding

# merge metadata files
mkdir repodata
head -n 2 repodata.adding/repomd.xml >repodata/repomd.xml
for file in filelists.xml other.xml primary.xml ; do
    # 1. take the 1st line only - to skip the line with number of packages which is not needed
    zcat repodata.adding/$file.gz | head -n 1 >repodata/$file
    # 2. take 2nd line with metadata tag and update the packages number in it
    packsold=0
    if [ -f repodata.base/$file.gz ] ; then
        packsold=`zcat repodata.base/$file.gz | head -n 2 | tail -n 1 | sed 's#.*packages="\(.*\)".*#\1#g'`
    fi
    packsnew=`zcat repodata.adding/$file.gz | head -n 2 | tail -n 1 | sed 's#.*packages="\(.*\)".*#\1#g'`
    packs=$(($packsold+$packsnew))
    zcat repodata.adding/$file.gz | head -n 2 | tail -n 1 | sed "s#packages=\".*\"#packages=\"$packs\"#g" >>repodata/$file
    # 3. take only 'package' tags from new file
    zcat repodata.adding/$file.gz | tail -n +3 | head -n -1 >>repodata/$file
    # 4. take only 'package' tags from old file if exists
    if [ -f repodata.base/$file.gz ] ; then
        zcat repodata.base/$file.gz | tail -n +3 | head -n -1 >>repodata/$file
    fi
    # 5. take the last closing line with metadata tag
    zcat repodata.adding/$file.gz | tail -n 1 >>repodata/$file

    # get the new data
    chsnew=`sha256sum repodata/$file | awk '{print $1}'`
    sz=`stat --printf="%s" repodata/$file`
    gzip repodata/$file
    chsgznew=`sha256sum repodata/$file.gz | awk '{print $1}'`
    szgz=`stat --printf="%s" repodata/$file.gz`
    timestamp=`date +%s -r repodata/$file.gz`

    # add info to repomd.xml file
    name=`echo $file | sed 's#\.xml$##g'`
    echo "<data type=\"$name\">" >>repodata/repomd.xml
    echo "  <checksum type=\"sha256\">$chsgznew</checksum>" >>repodata/repomd.xml
    echo "  <open-checksum type=\"sha256\">$chsnew</open-checksum>" >>repodata/repomd.xml
    echo "  <location href=\"repodata/$file.gz\"/>" >>repodata/repomd.xml
    echo "  <timestamp>$timestamp</timestamp>" >>repodata/repomd.xml
        echo "  <size>$szgz</size>" >>repodata/repomd.xml
        echo "  <open-size>$sz</open-size>" >>repodata/repomd.xml
    echo "</data>" >>repodata/repomd.xml
done
tail -n 1 repodata.adding/repomd.xml >>repodata/repomd.xml
gpg --detach-sign --armor repodata/repomd.xml

# copy the packages to S3
for file in $rpmpath/*.rpm ; do
    $aws s3 cp --acl public-read $file "$s3/$repopath/$file"
done

# update the metadata at the S3
$aws s3 sync --acl public-read repodata "$s3/$repopath/repodata"

# unlock the publishing
rm -rf $wslock

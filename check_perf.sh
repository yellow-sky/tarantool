#!/bin/bash
set -ex

patch_baseon=$1
if [ "$patch_baseon" == "" ]; then
    echo "Usage: $0 <git version/tag/branch>"
    echo "Examples:"
    echo "    $0 HEAD~250            # takes back 250 patches of the current HEAD"
    echo "    $0 2.1.2-66-g3b19db722 # checks 2.1.2-66-g3b19db722"
    echo "    $0 2.3.1               # checks tag 2.3.1 as 2.3.1-0-g5a1a220ee"
    echo "    $0 origin/2.2          # checks latest patch in 2.2 branch like 2.2.2-19-g2099b3f83"
    exit 1
fi

new_branch=avtikhon/gitlab-ci-check-perf

git fetch -p
git checkout -f master
git pull

git branch -D $new_branch || true
git branch $new_branch
git checkout $new_branch
git rebase --onto `git describe ${patch_baseon}` `git describe`

git show origin/master:.gitlab.mk >.gitlab.mk
git show origin/master:.gitlab-ci.yml >.gitlab-ci.yml
cp ../check_perf.sh .
git add check_perf.sh .gitlab-ci.yml .gitlab.mk
git commit -m "Check perf with $patch_baseon at $(git describe --long)"
git push -f --set-upstream origin $new_branch

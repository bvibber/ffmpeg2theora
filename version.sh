cd `dirname $0`
version='0.28'
test -e .svn && svnversion=`which svnversion`
echo -n $version
if [ "x$svnversion" != "x" ]; then
    echo -n "+svn"
    svnversion
else
    echo
fi

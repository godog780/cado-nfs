#!/usr/bin/env bash

# The main functions to compute SM are tested in utils/test_sm_utils.
# This test is here to check that the multi-threaded and mono-threaded versions
# give the same results and that the -nsm option works correctly.

SM="$1"
SOURCE_TEST_DIR="`dirname "$0"`"
TMPDIR=`mktemp -d /tmp/cadotest.XXXXXXXX`
# Make temp direcotry world-readable for easier debugging
chmod a+rx "${TMPDIR}"

poly="${SOURCE_TEST_DIR}/testsm.p59.poly"
purged="${SOURCE_TEST_DIR}/testsm.p59.purged.gz"
id="${SOURCE_TEST_DIR}/testsm.p59.index.gz"
go="2926718140519"
smexp="8565679074042993029589360"


args="-poly ${poly} -purged ${purged} -index ${id} -gorder ${go} -smexp1 ${smexp} -smexp0 0 -nsm0 0"

#without -nsm (ie nsm = deg F) and -t 1
"${SM}" ${args} -out "${TMPDIR}"/sm.5.1 -t 1
if [ "$?" -ne "0" ] ; then
  echo "$0: sm binary failed with -t 1 and without -nsm. Files remain in ${TMPDIR}"
  exit 1
fi
#without -nsm (ie nsm = deg F) and -t 2
"${SM}" ${args} -out "${TMPDIR}"/sm.5.2 -t 2
if [ "$?" -ne "0" ] ; then
  echo "$0: sm binary failed with -t 2 and without -nsm. Files remain in ${TMPDIR}"
  exit 1
fi

#with -nsm 2 and -t 1
"${SM}" ${args} -out "${TMPDIR}"/sm.2.1 -t 1 -nsm1 2
if [ "$?" -ne "0" ] ; then
  echo "$0: sm binary failed with -t 1 and -nsm1 2. Files remain in ${TMPDIR}"
  exit 1
fi
#with -nsm 2 and -t 2
"${SM}" ${args} -out "${TMPDIR}"/sm.2.2 -t 2 -nsm1 2
if [ "$?" -ne "0" ] ; then
  echo "$0: sm binary failed with -t 2 and -nsm1 2. Files remain in ${TMPDIR}"
  exit 1
fi


diff -b "${TMPDIR}"/sm.5.1 "${TMPDIR}"/sm.5.2 > /dev/null
if [ "$?" -ne "0" ] ; then
  echo "$0: Mono-threaded and multi-threaded versions do not match (without -nsm). Files remain in ${TMPDIR}"
  exit 1
fi

diff -b "${TMPDIR}"/sm.2.1 "${TMPDIR}"/sm.2.2 > /dev/null
if [ "$?" -ne "0" ] ; then
  echo "$0: Mono-threaded and multi-threaded versions do not match (with -nsm1 2). Files remain in ${TMPDIR}"
  exit 1
fi

cut -d " " -f 1-2 "${TMPDIR}"/sm.5.1 > "${TMPDIR}"/sm.5.1.short
diff -b "${TMPDIR}"/sm.5.1.short "${TMPDIR}"/sm.2.1 > /dev/null
if [ "$?" -ne "0" ] ; then
  echo "$0: First two SMs computed without -nsm do not match SMs computed with -nsm1 2). Files remain in ${TMPDIR}"
  exit 1
fi

rm -f "${TMPDIR}"/sm.5.1 "${TMPDIR}"/sm.5.2 "${TMPDIR}"/sm.2.1 "${TMPDIR}"/sm.2.2
rm -f "${TMPDIR}"/sm.5.1.short
rmdir "${TMPDIR}"

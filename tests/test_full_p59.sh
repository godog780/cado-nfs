#!/bin/sh

CADO_NFS_SOURCE_DIR=$1

t=`mktemp -d /tmp/cado-check.XXXXXXX`
${CADO_NFS_SOURCE_DIR}/factor.sh 43341748620473677010074177283795146221310971425909898235183 -dlp -t 2 && rm -rf $t

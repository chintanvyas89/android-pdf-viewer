#!/bin/sh
# make sure ndk-build is in path

SCRIPTDIR=`dirname $0`
MUPDF_FILE=mupdf-1.1-source.tar.gz
MUPDF=mupdf-1.1-source
FREETYPE=freetype-2.4.10
OPENJPEG=openjpeg-1.5.1
JBIG2DEC=jbig2dec-0.11
JPEGSRC=jpegsrc.v8d.tar.gz
JPEGDIR=jpeg-8d

cd $SCRIPTDIR/../deps
tar xvf $FREETYPE.tar.bz2
tar xvf $JPEGSRC
tar xvf $MUPDF_FILE
tar xvf $OPENJPEG.tar.gz
tar xvf $JBIG2DEC.tar.gz
cp $OPENJPEG/libopenjpeg/*.[ch] ../jni/openjpeg/
echo '#define PACKAGE_VERSION' '"'$OPENJPEG'"' > ../jni/openjpeg/opj_config.h
cp $JPEGDIR/*.[ch] ../jni/jpeg/
cp $JBIG2DEC/* ../jni/jbig2dec/
for x in draw fitz pdf ; do
    cp -r $MUPDF/$x/*.[ch] ../jni/mupdf/$x/
done
cp ../jni/mupdf/fitz/apv_fitz.h ../jni/mupdf/fitz/fitz.h
cp -r $MUPDF/fonts ../jni/mupdf/
cp -r $FREETYPE/{src,include} ../jni/freetype/
gcc -o ../scripts/fontdump $MUPDF/scripts/fontdump.c
cd ../jni/mupdf
mkdir generated 2> /dev/null
../../scripts/fontdump generated/font_base14.h fonts/*.cff
../../scripts/fontdump generated/font_droid.h fonts/droid/DroidSans.ttf fonts/droid/DroidSansMono.ttf
cd ..
ndk-build

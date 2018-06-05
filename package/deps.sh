#!/bin/sh

arch=${1:-i686}

if [ "$arch" == "i686" ]; then
    bits=32
elif [ "$arch" == "x86_64" ]; then
    bits=64
else
    echo "Error: unrecognized architecture $arch"
    exit 1
fi

iface=${2:-qt5}

# Note: This script is written to be used with the Fedora mingw environment
MINGWROOT=/usr/$arch-w64-mingw32/sys-root/mingw

optflags="-pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions --param=ssp-buffer-size=4"

# Halt on errors
set -e

if [ "$3" == "debug" ]; then
    withdebug=1
    optflags+=" -O0 -g  -fno-omit-frame-pointer"
else
    optflags+=" -O2 -fomit-frame-pointer"
fi

export MINGW32_CFLAGS="$optflags"
export MINGW32_CXXFLAGS="$optflags"
export MINGW64_CFLAGS="$optflags"
export MINGW64_CXXFLAGS="$optflags"

win32dir="$(dirname $(readlink -f $0))"
srcdir="$win32dir/.."
builddir="$win32dir/../build/mingw$bits-$iface"
installroot="$builddir/root"
#Build

if [ "$4" != "dep" ]; then
rm -rf $builddir
mkdir -p $builddir
pushd $builddir > /dev/null
mingw$bits-cmake -DINTERFACE_TYPE=$iface ../../
mingw$bits-make
mkdir -p $installroot/bin && mv $builddir/*.exe $installroot/bin
fi

# Collect dependencies
function isnativedll {
    # If the import library exists but not the dynamic library, the dll ist most likely a native one
    local lower=${1,,}
    [ ! -e $MINGWROOT/bin/$1 ] && [ -f $MINGWROOT/lib/lib${lower/%.*/.a} ] && return 0;
    return 1;
}

function linkDep {
# Link the specified binary dependency and it's dependencies
    local destdir="$installroot/${2:-$(dirname $1)}"
    local name="$(basename $1)"
    test -e "$destdir/$name" && return 0
    echo "Linking $1..."
    [ ! -e "$MINGWROOT/$1" ] && (echo "Error: missing $MINGWROOT/$1"; return 1)
    mkdir -p "$destdir" || return 1
    cp "$MINGWROOT/$1" "$destdir/$name" || return 1
    autoLinkDeps $destdir/$name || return 1
    if [ $withdebug ]; then
        [ -e "$MINGWROOT/$1.debug" ] && ln -sf "$MINGWROOT/$1.debug" "$destdir/$name.debug" || echo "Warning: missing $name.debug"
    fi
    return 0
}

function autoLinkDeps {
# Collects and links the dependencies of the specified binary
    for dep in $(mingw-objdump -p "$1" | grep "DLL Name" | awk '{print $3}'); do
        if ! isnativedll "$dep"; then
            linkDep bin/$dep || return 1
        fi
    done
    return 0
}
autoLinkDeps $installroot/bin/EndProcess.exe
#linkDep bin/gdb.exe
#linkDep bin/twaindsm.dll

linkDep lib/qt5/plugins/imageformats/qgif.dll  bin/imageformats
linkDep lib/qt5/plugins/imageformats/qicns.dll bin/imageformats
linkDep lib/qt5/plugins/imageformats/qico.dll  bin/imageformats
linkDep lib/qt5/plugins/imageformats/qjp2.dll  bin/imageformats
linkDep lib/qt5/plugins/imageformats/qjpeg.dll bin/imageformats
linkDep lib/qt5/plugins/imageformats/qtga.dll  bin/imageformats
linkDep lib/qt5/plugins/imageformats/qtiff.dll bin/imageformats
linkDep lib/qt5/plugins/imageformats/qwbmp.dll bin/imageformats
linkDep lib/qt5/plugins/imageformats/qwebp.dll bin/imageformats
linkDep lib/qt5/plugins/platforms/qwindows.dll bin/platforms

# cp shared tess data and poppler data
mkdir -p  "$installroot/share/tessdata/" &&  cp "/usr/share/tesseract/4/tessdata/chi_sim.traineddata" "$installroot/share/tessdata/"
mkdir -p "$installroot/share/poppler/" && cp -r "/usr/share/poppler/" "$installroot/share/"

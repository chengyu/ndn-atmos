#!/usr/bin/env bash
set -x
set -e

pushd /tmp >/dev/null

INSTALLED_VERSION=$((cd ndn-cxx && git rev-parse HEAD) 2>/dev/null || echo NONE)

sudo rm -Rf ndn-cxx-latest
git clone --branch ndn-cxx-0.3.3 git://github.com/named-data/ndn-cxx ndn-cxx-0.3.3
LATEST_VERSION=$((cd ndn-cxx-latest && git rev-parse HEAD) 2>/dev/null || echo UNKNOWN)

if [[ $INSTALLED_VERSION != $LATEST_VERSION ]]; then
    sudo rm -Rf ndn-cxx
    mv ndn-cxx-0.3.3 ndn-cxx
else
    sudo rm -Rf ndn-cxx-0.3.3
fi

sudo rm -Rf /usr/local/include/ndn-cxx
sudo rm -f /usr/local/lib/libndn-cxx*
sudo rm -f /usr/local/lib/pkgconfig/libndn-cxx*

pushd ndn-cxx >/dev/null

git checkout -b shared_library 7ed294302beee4979e97ff338dee0eb3eef51142
./waf configure -j1 --color=yes --without-osx-keychain --disable-static --enable-shared
./waf -j1 --color=yes
sudo ./waf install -j1 --color=yes

(echo -e '/usr/local/lib\n/usr/local/lib64' | sudo tee /etc/ld.so.conf.d/ndn-cxx.conf) || true
sudo ldconfig || true

popd >/dev/null
popd >/dev/null

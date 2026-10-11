#!/usr/bin/env bash
set -e

INFO='\033[1;34m'
SUCCESS='\033[1;32m'
WARNING='\033[1;33m'
ERROR='\033[1;31m'
NC='\033[0m'

AUTO_YES=0

for arg in "$@"; do
    case $arg in
        -y|--yes)
            AUTO_YES=1
            ;;
    esac
done

# =========================
# check brew
# =========================
if ! command -v brew &> /dev/null; then
    echo -e "${ERROR}Homebrew not found 💀 install it first${NC}"
    exit 1
fi

echo -e "${INFO}---> Checking dependencies...${NC}"

REQUIRED_PACKAGES=(
    cmake
    ninja
    git
    sdl2
    sdl2_image
    ccache
)

PACKAGES_TO_INSTALL=()

for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if brew list "$pkg" &> /dev/null; then
        echo -e "  [${SUCCESS}✓${NC}] $pkg"
    else
        echo -e "  [${WARNING}✗${NC}] $pkg"
        PACKAGES_TO_INSTALL+=("$pkg")
    fi
done

# =========================
# install deps
# =========================
if [ ${#PACKAGES_TO_INSTALL[@]} -ne 0 ]; then
    echo ""
    echo -e "${WARNING}Missing brew packages${NC}"

    if [ $AUTO_YES -eq 1 ]; then
        confirm="y"
    else
        read -p "Install now? (y/n): " confirm
    fi

    if [[ "$confirm" =~ ^[yY]([eE][sS])?$ ]]; then
        brew install "${PACKAGES_TO_INSTALL[@]}"
    else
        echo -e "${ERROR}Aborted${NC}"
        exit 1
    fi
fi

# =========================
# clean build
# =========================
echo -e "${INFO}---> Cleaning build folder...${NC}"
rm -rf build
mkdir build

# =========================
# icon build (optional)
# =========================
if [ -f "resources/new-logo.png" ]; then
    echo -e "${INFO}---> Building icons...${NC}"

    ICONSET="AppIcon.iconset"
    IMG="resources/new-logo.png"

    rm -rf "$ICONSET"
    mkdir "$ICONSET"

    sips -z 16 16 "$IMG" --out "$ICONSET/icon_16x16.png"
    sips -z 32 32 "$IMG" --out "$ICONSET/icon_16x16@2x.png"
    sips -z 32 32 "$IMG" --out "$ICONSET/icon_32x32.png"
    sips -z 64 64 "$IMG" --out "$ICONSET/icon_32x32@2x.png"
    sips -z 128 128 "$IMG" --out "$ICONSET/icon_128x128.png"
    sips -z 256 256 "$IMG" --out "$ICONSET/icon_128x128@2x.png"
    sips -z 256 256 "$IMG" --out "$ICONSET/icon_256x256.png"
    sips -z 512 512 "$IMG" --out "$ICONSET/icon_256x256@2x.png"
    sips -z 512 512 "$IMG" --out "$ICONSET/icon_512x512.png"
    sips -z 1024 1024 "$IMG" --out "$ICONSET/icon_512x512@2x.png"

    iconutil -c icns "$ICONSET" -o AppIcon.icns

    mkdir -p MacResources
    cp AppIcon.icns MacResources/
fi

# =========================
# cmake configure
# =========================
echo -e "${INFO}---> Configuring CMake...${NC}"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_EXECUTABLE=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

echo -e "${SUCCESS}CMake OK${NC}"

# =========================
# build
# =========================
echo -e "${INFO}---> Building...${NC}"

cmake --build build -- -j"$(sysctl -n hw.ncpu)"

echo -e "${SUCCESS}Build complete ✔${NC}"

# =========================
# DMG builder (optional)
# =========================
if [ -d "build" ]; then
    echo -e "${INFO}---> Searching .app...${NC}"

    APP=$(find build -name "*.app" | head -n 1)

    if [ -z "$APP" ]; then
        echo -e "${ERROR}No .app found 💀${NC}"
        exit 1
    fi

    echo -e "${SUCCESS}Found: $APP${NC}"

    read -p "Build DMG? (y/n): " dmg_confirm

    if [[ "$dmg_confirm" =~ ^[yY]([eE][sS])?$ ]]; then
        echo -e "${INFO}---> Creating DMG...${NC}"

        rm -rf dmg
        mkdir dmg

        cp -r "$APP" dmg/
        ln -s /Applications dmg/Applications

        DMG_NAME="CasioEmuMsvc.dmg"

        hdiutil create "$DMG_NAME" \
            -volname "CasioEmuMsvc" \
            -srcfolder dmg \
            -ov \
            -format UDZO

        echo -e "${SUCCESS}DMG created: $DMG_NAME ✔${NC}"
    else
        echo -e "${WARNING}Skipping DMG build${NC}"
    fi
fi
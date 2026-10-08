MiniGLV3D 29.1 -- OpenGL 1.1 driver sources for Broadcom V3D 4.2 on AmigaOS 68k

Builds libminiglv3d.a, the archive minigl-shared-library links into
minigl.library as third_party/pistorm3d/libminiglv3d.a.

    make                 -O2, as shipped
    make OPT=-O3
    make BUILDNUM=42     stamps 42 into the GL_RENDERER string
    make clean

Needs the m68k-amigaos-gcc toolchain. The Makefile header states which flags
are REQUIRED rather than preferred, and why -- -fno-strict-aliasing in
particular is not optional.

29 .c and 26 .h: exactly the files the build reads, nothing else. The renderer
string reports build number 0 unless BUILDNUM is set.

Licence: Licence.txt (Hyperion MiniGL Open Source License).

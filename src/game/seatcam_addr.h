/*
 * seatcam_addr.h - the guest addresses of the VR seat camera (seatcam.c), each with its three discs' values like the rest
 * of the port (region.h: NTSC-U, PAL Fr/De/It, PAL En/Es/Nl/Sv). How they were found, and how to find the missing ones for
 * a PAL disc, is in docs/vr.md ("Addresses").
 *
 * The first four already have their PAL values in the port (hirefresh.c, autopilot.c, controls.c, main.c). SC_RFLAGS has
 * none yet, so a build for a PAL disc leaves the seat camera off (SC_SUPPORTED) and says so, instead of writing to an
 * address nobody checked.
 */
#ifndef BO3_SEATCAM_ADDR_H
#define BO3_SEATCAM_ADDR_H

#include "region.h"

#define SC_CAR1    BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u)    /* player 1's CB3PlayerCar (autopilot.c) */
#define SC_VIEW    BO3_VA(0x004D6200u, 0x004D6520u, 0x004D65C0u)    /* the view object (hirefresh.c); + 0x58 its RwCamera */
#define SC_PADS    BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)    /* the pad manager's four records (controls.c) */
#define SC_D3D_PUSH BO3_VA(0x0035FB48u, 0x0035FE48u, 0x0035FEE8u)   /* the Direct3D device's push buffer (hirefresh.c) */
/* The render flags (12 bloom, 14 motion blur): found on the NTSC-U disc only (docs/vr.md, "Addresses"). */
#if defined(BO3_REGION_PAL) && BO3_REGION_PAL
#define SC_SUPPORTED 0
#define SC_RFLAGS    0u
#else
#define SC_SUPPORTED 1
#define SC_RFLAGS    0x004D61C4u    /* region: not an address (NTSC-U only: no PAL value found yet) */
#endif

#endif

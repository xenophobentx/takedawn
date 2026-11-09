/* xr-probe.c: what does the active OpenXR runtime (SteamVR) offer, and does it accept a
 * Vulkan instance and device made the way the OpenXR spec says (XR_KHR_vulkan_enable2)?
 * Read-only: it draws nothing and creates no session.
 *
 *   xr-probe            stage 1: the runtime's extensions (does not need a headset)
 *   xr-probe --system   also stages 2 and 3: an OpenXR instance, the headset's system and its
 *                       recommended eye size, the Vulkan versions it wants, then a Vulkan
 *                       instance and device made THROUGH the runtime. Starts SteamVR if it is
 *                       not running, and wants the headset connected.
 *
 * The OpenXR loader and vulkan-1 are loaded at run time, as the game does for Vulkan.
 *
 * Build in Git Bash after tools/win/fetch-deps.sh and fetching the OpenXR headers (docs/vr-openxr-proposal.md):
 *   D=$HOME/.cache/bo3-windeps
 *   $D/llvm-mingw/bin/x86_64-w64-mingw32-clang -I$D/openxr/include -I$D/vulkan/include \
 *       -DXR_USE_GRAPHICS_API_VULKAN -DXR_USE_PLATFORM_WIN32 xr-probe.c -o xr-probe.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <d3d11.h>
#include <dxgi.h>
#include <openxr/openxr_platform.h>

static PFN_xrGetInstanceProcAddr p_getproc;
static XrInstance inst;

#define LOAD(var, name) do { if (XR_FAILED(p_getproc(inst, name, (PFN_xrVoidFunction *)&var))) \
        { printf("  missing %s\n", name); return 0; } } while (0)

static const char *res(XrResult r)
{
    static char b[64];

    if (inst) {
        PFN_xrResultToString f = NULL;

        p_getproc(inst, "xrResultToString", (PFN_xrVoidFunction *)&f);
        if (f && XR_SUCCEEDED(f(inst, r, b)))
            return b;
    }
    snprintf(b, sizeof b, "XrResult %d", (int)r);
    return b;
}

static int stage1(PFN_xrEnumerateInstanceExtensionProperties enum_ext, int *has_vk2)
{
    uint32_t n = 0, i;
    XrExtensionProperties *p;
    static const char *want[] = {
        "XR_KHR_vulkan_enable2", "XR_KHR_vulkan_enable", "XR_KHR_composition_layer_depth",
        "XR_KHR_composition_layer_cylinder", "XR_KHR_composition_layer_equirect2",
        "XR_KHR_visibility_mask", "XR_KHR_win32_convert_performance_counter_time",
        "XR_EXT_hand_tracking", "XR_FB_display_refresh_rate", "XR_EXT_performance_settings",
        "XR_META_foveation_eye_tracked", "XR_FB_foveation", "XR_KHR_binding_modification",
        "XR_EXT_debug_utils", "XR_KHR_vulkan_swapchain_format_list",
    };

    if (XR_FAILED(enum_ext(NULL, 0, &n, NULL)) || !n) {
        printf("stage 1: the runtime lists no instance extensions\n");
        return 0;
    }
    p = calloc(n, sizeof *p);
    for (i = 0; i < n; i++)
        p[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    enum_ext(NULL, n, &n, p);
    printf("stage 1: %u instance extensions\n", n);
    for (i = 0; i < sizeof want / sizeof want[0]; i++) {
        uint32_t k, v = 0;

        for (k = 0; k < n; k++)
            if (!strcmp(p[k].extensionName, want[i]))
                v = p[k].extensionVersion;
        printf("  %-48s %s\n", want[i], v ? "yes" : "no");
        if (!strcmp(want[i], "XR_KHR_vulkan_enable2"))
            *has_vk2 = v != 0;
    }
    if (getenv("XR_PROBE_ALL")) {
        for (i = 0; i < n; i++)
            printf("    %s\n", p[i].extensionName);
    }
    free(p);
    return 1;
}


/* Stage 4 (--swapchain): a session on the device just made, then swapchains of several shapes, each announced before it is
 * made (a crash inside the runtime names its trial). Shows the headset a moment: nothing is submitted, no frame is shown. */
static int stage4(XrSystemId sys, VkInstance vki, VkPhysicalDevice pd, VkDevice vkd, uint32_t qf, int v1)
{
    PFN_xrCreateSession create_session = NULL;
    PFN_xrDestroySession destroy_session = NULL;
    PFN_xrPollEvent poll = NULL;
    PFN_xrBeginSession begin = NULL;
    PFN_xrEnumerateSwapchainFormats enum_fmts = NULL;
    PFN_xrCreateSwapchain create_sc = NULL;
    PFN_xrDestroySwapchain destroy_sc = NULL;
    PFN_xrEnumerateSwapchainImages enum_imgs = NULL;
    XrGraphicsBindingVulkan2KHR gb;
    XrSessionCreateInfo si;
    XrSession sess = XR_NULL_HANDLE;
    XrResult r;
    int running = 0, i;
    DWORD t0 = GetTickCount();
    static const struct { int64_t fmt; uint32_t w, h; XrSwapchainUsageFlags use; const char *what; } trial[] = {
        { 50, 2560, 1920, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT, "B8G8R8A8_SRGB 2560x1920 colour+transfer dst+sampled (the game's)" },
        { 50, 2560, 1920, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT, "B8G8R8A8_SRGB 2560x1920 colour+transfer dst" },
        { 50, 2560, 1920, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT, "B8G8R8A8_SRGB 2560x1920 colour only" },
        { 43, 2560, 1920, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT, "R8G8B8A8_SRGB 2560x1920" },
        { 50, 2528, 2704, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT, "B8G8R8A8_SRGB 2528x2704 (the recommended eye size)" },
        { 50, 1280, 960, XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT, "B8G8R8A8_SRGB 1280x960" },
    };

    LOAD(create_session, "xrCreateSession");
    LOAD(destroy_session, "xrDestroySession");
    LOAD(poll, "xrPollEvent");
    LOAD(begin, "xrBeginSession");
    LOAD(enum_fmts, "xrEnumerateSwapchainFormats");
    LOAD(create_sc, "xrCreateSwapchain");
    LOAD(destroy_sc, "xrDestroySwapchain");
    LOAD(enum_imgs, "xrEnumerateSwapchainImages");
    memset(&gb, 0, sizeof gb);
    gb.type = v1 ? XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR : XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
    gb.instance = vki;
    gb.physicalDevice = pd;
    gb.device = vkd;
    gb.queueFamilyIndex = qf;
    gb.queueIndex = 0;
    memset(&si, 0, sizeof si);
    si.type = XR_TYPE_SESSION_CREATE_INFO;
    si.next = &gb;
    si.systemId = sys;
    printf("stage 4: xrCreateSession...\n");
    r = create_session(inst, &si, &sess);
    printf("  -> %s\n", res(r));
    if (XR_FAILED(r))
        return 0;
    while (!running && GetTickCount() - t0 < 8000) {
        XrEventDataBuffer ev;

        memset(&ev, 0, sizeof ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (poll(inst, &ev) == XR_SUCCESS && ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *e = (const XrEventDataSessionStateChanged *)&ev;

            printf("  session state %d\n", (int)e->state);
            if (e->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi;

                memset(&bi, 0, sizeof bi);
                bi.type = XR_TYPE_SESSION_BEGIN_INFO;
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                r = begin(sess, &bi);
                printf("  xrBeginSession -> %s\n", res(r));
                running = XR_SUCCEEDED(r);
            }
        } else
            Sleep(20);
    }
    {
        uint32_t n = 0, k;
        int64_t *f;

        enum_fmts(sess, 0, &n, NULL);
        f = (int64_t *)calloc(n ? n : 1, sizeof *f);
        enum_fmts(sess, n, &n, f);
        printf("  the runtime offers %u swapchain formats:", n);
        for (k = 0; k < n; k++)
            printf(" %lld", (long long)f[k]);
        printf("\n");
        free(f);
    }
    for (i = 0; i < (int)(sizeof trial / sizeof trial[0]); i++) {
        XrSwapchainCreateInfo ci;
        XrSwapchain sc = XR_NULL_HANDLE;
        uint32_t n = 0;

        memset(&ci, 0, sizeof ci);
        ci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        ci.usageFlags = trial[i].use;
        ci.format = trial[i].fmt;
        ci.sampleCount = 1;
        ci.width = trial[i].w;
        ci.height = trial[i].h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        if (getenv("XR_TRIAL") && atoi(getenv("XR_TRIAL")) != i + 1)
            continue;                    /* XR_TRIAL=n: only that one (a crash ends the process) */
        printf("  trial %d: %s ...\n", i + 1, trial[i].what);
        r = create_sc(sess, &ci, &sc);
        if (XR_SUCCEEDED(r)) {
            enum_imgs(sc, 0, &n, NULL);
            printf("    -> ok, %u images\n", n);
            destroy_sc(sc);
        } else
            printf("    -> %s\n", res(r));
    }
    destroy_session(sess);
    return 1;
}


/* XR_V1=1 (with --system --swapchain): the older XR_KHR_vulkan_enable way. The runtime only names the extensions; this
 * makes the Vulkan instance and device itself, then the session and the swapchain trials. Whether swapchain creation
 * survives a device the runtime did not make. */
static int split_exts(char *list, const char **out, int max)
{
    int n = 0;
    char *t = strtok(list, " ");

    while (t && n < max) {
        out[n++] = t;
        t = strtok(NULL, " ");
    }
    return n;
}

static int stage_v1(void)
{
    XrInstanceCreateInfo ci;
    XrSystemGetInfo gi;
    XrSystemId sys = 0;
    XrResult r;
    static const char *xe[] = { "XR_KHR_vulkan_enable" };
    PFN_xrGetVulkanGraphicsRequirementsKHR get_req = NULL;
    PFN_xrGetVulkanInstanceExtensionsKHR inst_exts = NULL;
    PFN_xrGetVulkanDeviceExtensionsKHR dev_exts = NULL;
    PFN_xrGetVulkanGraphicsDeviceKHR get_dev = NULL;
    PFN_xrCreateInstance mk = NULL;
    XrGraphicsRequirementsVulkanKHR req;
    HMODULE vk;
    PFN_vkGetInstanceProcAddr vkgipa;
    char ibuf[2048], dbuf[2048];
    const char *iex[24], *dex[24];
    uint32_t n = 0;
    int ni, nd;
    VkApplicationInfo app;
    VkInstanceCreateInfo vci;
    VkInstance vki = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    PFN_vkCreateInstance vkCreateInst;
    PFN_vkCreateDevice vkCreateDev;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties gqf;
    VkQueueFamilyProperties qp[16];
    uint32_t nq = 0, qf = 0, k;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi;
    VkDeviceCreateInfo dci;
    VkDevice vkd = VK_NULL_HANDLE;

    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    strcpy(ci.applicationInfo.applicationName, "xr-probe");
    strcpy(ci.applicationInfo.engineName, "none");
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = xe;
    p_getproc(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&mk);
    r = mk(&ci, &inst);
    printf("v1: xrCreateInstance(XR_KHR_vulkan_enable) -> %s\n", XR_FAILED(r) ? "failed" : "ok");
    if (XR_FAILED(r))
        return 0;
    LOAD(get_req, "xrGetVulkanGraphicsRequirementsKHR");
    LOAD(inst_exts, "xrGetVulkanInstanceExtensionsKHR");
    LOAD(dev_exts, "xrGetVulkanDeviceExtensionsKHR");
    LOAD(get_dev, "xrGetVulkanGraphicsDeviceKHR");
    {
        PFN_xrGetSystem gs = NULL;

        LOAD(gs, "xrGetSystem");
        memset(&gi, 0, sizeof gi);
        gi.type = XR_TYPE_SYSTEM_GET_INFO;
        gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        r = gs(inst, &gi, &sys);
        if (XR_FAILED(r)) {
            printf("v1: xrGetSystem failed\n");
            return 0;
        }
    }
    memset(&req, 0, sizeof req);
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
    r = get_req(inst, sys, &req);
    printf("v1: requirements -> %s, Vulkan %u.%u to %u.%u\n", XR_FAILED(r) ? "failed" : "ok",
           XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
           XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported));
    inst_exts(inst, sys, sizeof ibuf, &n, ibuf);
    printf("v1: instance extensions wanted: %s\n", ibuf);
    ni = split_exts(ibuf, iex, 24);
    vk = LoadLibraryA("vulkan-1.dll");
    vkgipa = vk ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr") : NULL;
    if (!vkgipa)
        return 0;
    vkCreateInst = (PFN_vkCreateInstance)vkgipa(VK_NULL_HANDLE, "vkCreateInstance");
    memset(&app, 0, sizeof app);
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_3;
    memset(&vci, 0, sizeof vci);
    vci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vci.pApplicationInfo = &app;
    vci.enabledExtensionCount = (uint32_t)ni;
    vci.ppEnabledExtensionNames = iex;
    printf("v1: vkCreateInstance -> %d\n", (int)vkCreateInst(&vci, NULL, &vki));
    r = get_dev(inst, sys, vki, &pd);
    printf("v1: the headset's GPU -> %s\n", XR_FAILED(r) ? "failed" : "ok");
    dev_exts(inst, sys, sizeof dbuf, &n, dbuf);
    printf("v1: device extensions wanted: %s\n", dbuf);
    nd = split_exts(dbuf, dex, 24);
    gqf = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkgipa(vki, "vkGetPhysicalDeviceQueueFamilyProperties");
    nq = 16;
    gqf(pd, &nq, qp);
    for (k = 0; k < nq; k++)
        if (qp[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            qf = k;
            break;
        }
    memset(&qi, 0, sizeof qi);
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    memset(&dci, 0, sizeof dci);
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qi;
    dci.enabledExtensionCount = (uint32_t)nd;
    dci.ppEnabledExtensionNames = dex;
    {
        /* XR_FEAT=1: the features a Vulkan 1.2 device has to switch on for what the extensions give. */
        static VkPhysicalDeviceVulkan12Features f12;
        static VkPhysicalDeviceVulkan11Features f11;

        if (getenv("XR_FEAT")) {
            memset(&f12, 0, sizeof f12);
            f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            f12.timelineSemaphore = VK_TRUE;
            memset(&f11, 0, sizeof f11);
            f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            f11.pNext = &f12;
            dci.pNext = &f11;
            printf("v1: device created with timelineSemaphore on\n");
        }
    }
    vkCreateDev = (PFN_vkCreateDevice)vkgipa(vki, "vkCreateDevice");
    printf("v1: vkCreateDevice -> %d\n", (int)vkCreateDev(pd, &dci, NULL, &vkd));
    if (!vkd)
        return 0;
    return stage4(sys, vki, pd, vkd, qf, 1);
}

/* XR_D3D11=1 (with --system --swapchain): the same swapchain trials through Direct3D 11, to see whether the crash is
 * in the runtime's swapchain creation for any graphics API or only for Vulkan. */
static int stage_d3d11(void)
{
    XrInstanceCreateInfo ci;
    XrSystemGetInfo gi;
    XrSystemId sys = 0;
    XrResult r;
    static const char *xe[] = { "XR_KHR_D3D11_enable" };
    PFN_xrCreateInstance mk = NULL;
    PFN_xrGetSystem gs = NULL;
    PFN_xrGetD3D11GraphicsRequirementsKHR get_req = NULL;
    PFN_xrCreateSession create_session = NULL;
    PFN_xrDestroySession destroy_session = NULL;
    PFN_xrPollEvent poll = NULL;
    PFN_xrBeginSession begin = NULL;
    PFN_xrCreateSwapchain create_sc = NULL;
    PFN_xrDestroySwapchain destroy_sc = NULL;
    PFN_xrEnumerateSwapchainImages enum_imgs = NULL;
    XrGraphicsRequirementsD3D11KHR req;
    IDXGIFactory1 *fac = NULL;
    IDXGIAdapter1 *ad = NULL, *pick = NULL;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    XrGraphicsBindingD3D11KHR gb;
    XrSessionCreateInfo si;
    XrSession sess = XR_NULL_HANDLE;
    DWORD t0 = GetTickCount();
    int running = 0, k;

    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    strcpy(ci.applicationInfo.applicationName, "xr-probe");
    strcpy(ci.applicationInfo.engineName, "none");
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = xe;
    p_getproc(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&mk);
    r = mk(&ci, &inst);
    printf("d3d11: xrCreateInstance(XR_KHR_D3D11_enable) -> %s\n", XR_FAILED(r) ? "failed" : "ok");
    if (XR_FAILED(r))
        return 0;
    LOAD(gs, "xrGetSystem");
    LOAD(get_req, "xrGetD3D11GraphicsRequirementsKHR");
    LOAD(create_session, "xrCreateSession");
    LOAD(destroy_session, "xrDestroySession");
    LOAD(poll, "xrPollEvent");
    LOAD(begin, "xrBeginSession");
    LOAD(create_sc, "xrCreateSwapchain");
    LOAD(destroy_sc, "xrDestroySwapchain");
    LOAD(enum_imgs, "xrEnumerateSwapchainImages");
    memset(&gi, 0, sizeof gi);
    gi.type = XR_TYPE_SYSTEM_GET_INFO;
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(gs(inst, &gi, &sys)))
        return 0;
    memset(&req, 0, sizeof req);
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
    r = get_req(inst, sys, &req);
    printf("d3d11: requirements -> %s (adapter luid %08lx:%08lx)\n", XR_FAILED(r) ? "failed" : "ok",
           (unsigned long)req.adapterLuid.HighPart, (unsigned long)req.adapterLuid.LowPart);
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&fac)))
        return 0;
    for (k = 0; fac->lpVtbl->EnumAdapters1(fac, (UINT)k, &ad) == S_OK; k++) {
        DXGI_ADAPTER_DESC1 d;

        ad->lpVtbl->GetDesc1(ad, &d);
        if (d.AdapterLuid.LowPart == req.adapterLuid.LowPart && d.AdapterLuid.HighPart == req.adapterLuid.HighPart)
            pick = ad;
    }
    if (!pick || FAILED(D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, &fl, 1,
                                          D3D11_SDK_VERSION, &dev, NULL, &ctx))) {
        printf("d3d11: no device\n");
        return 0;
    }
    memset(&gb, 0, sizeof gb);
    gb.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
    gb.device = dev;
    memset(&si, 0, sizeof si);
    si.type = XR_TYPE_SESSION_CREATE_INFO;
    si.next = &gb;
    si.systemId = sys;
    r = create_session(inst, &si, &sess);
    printf("d3d11: xrCreateSession -> %s\n", XR_FAILED(r) ? "failed" : "ok");
    if (XR_FAILED(r))
        return 0;
    while (!running && GetTickCount() - t0 < 8000) {
        XrEventDataBuffer ev;

        memset(&ev, 0, sizeof ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (poll(inst, &ev) == XR_SUCCESS && ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED &&
            ((const XrEventDataSessionStateChanged *)&ev)->state == XR_SESSION_STATE_READY) {
            XrSessionBeginInfo bi;

            memset(&bi, 0, sizeof bi);
            bi.type = XR_TYPE_SESSION_BEGIN_INFO;
            bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            running = XR_SUCCEEDED(begin(sess, &bi));
            printf("d3d11: session running: %d\n", running);
        } else
            Sleep(20);
    }
    {
        XrSwapchainCreateInfo sc;
        XrSwapchain h = XR_NULL_HANDLE;
        uint32_t n = 0;

        memset(&sc, 0, sizeof sc);
        sc.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = 29;                  /* DXGI_FORMAT_R8G8B8A8_UNORM_SRGB */
        sc.sampleCount = 1;
        sc.width = 1280;
        sc.height = 960;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        printf("d3d11: xrCreateSwapchain 1280x960 ...\n");
        r = create_sc(sess, &sc, &h);
        if (XR_SUCCEEDED(r)) {
            enum_imgs(h, 0, &n, NULL);
            printf("d3d11:   -> ok, %u images\n", n);
            destroy_sc(h);
        } else
            printf("d3d11:   -> failed %d\n", (int)r);
    }
    destroy_session(sess);
    return 1;
}

static int stage23(int swapchain)
{
    XrInstanceCreateInfo ci;
    XrSystemGetInfo gi;
    XrSystemId sys = 0;
    XrSystemProperties sp;
    XrResult r;
    static const char *exts[] = { "XR_KHR_vulkan_enable2" };
    PFN_xrEnumerateViewConfigurationViews enum_views = NULL;
    PFN_xrGetSystem get_sys = NULL;
    PFN_xrGetSystemProperties get_props = NULL;
    PFN_xrGetInstanceProperties get_iprops = NULL;
    PFN_xrGetVulkanGraphicsRequirements2KHR get_req = NULL;
    PFN_xrCreateVulkanInstanceKHR mk_inst = NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR get_dev = NULL;
    PFN_xrCreateVulkanDeviceKHR mk_dev = NULL;
    XrViewConfigurationView views[2];
    uint32_t nv = 0;
    XrInstanceProperties ip;
    XrGraphicsRequirementsVulkan2KHR req;
    HMODULE vk;
    PFN_vkGetInstanceProcAddr vkgipa;

    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    strcpy(ci.applicationInfo.applicationName, "xr-probe");
    strcpy(ci.applicationInfo.engineName, "none");
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    {
        PFN_xrCreateInstance mk = NULL;

        p_getproc(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&mk);
        r = mk(&ci, &inst);
    }
    if (XR_FAILED(r)) {
        printf("stage 2: xrCreateInstance failed: %s (%d)\n", res(r), (int)r);
        return 0;
    }
    LOAD(get_sys, "xrGetSystem");
    LOAD(get_props, "xrGetSystemProperties");
    LOAD(get_iprops, "xrGetInstanceProperties");
    LOAD(enum_views, "xrEnumerateViewConfigurationViews");
    LOAD(get_req, "xrGetVulkanGraphicsRequirements2KHR");
    LOAD(mk_inst, "xrCreateVulkanInstanceKHR");
    LOAD(get_dev, "xrGetVulkanGraphicsDevice2KHR");
    LOAD(mk_dev, "xrCreateVulkanDeviceKHR");

    memset(&ip, 0, sizeof ip);
    ip.type = XR_TYPE_INSTANCE_PROPERTIES;
    get_iprops(inst, &ip);
    printf("stage 2: runtime \"%s\" version %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
           XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    memset(&gi, 0, sizeof gi);
    gi.type = XR_TYPE_SYSTEM_GET_INFO;
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = get_sys(inst, &gi, &sys);
    if (XR_FAILED(r)) {
        printf("  xrGetSystem: %s: no headset system (is the headset connected and SteamVR running?)\n", res(r));
        return 0;
    }
    memset(&sp, 0, sizeof sp);
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    get_props(inst, sys, &sp);
    printf("  system \"%s\": max swapchain %ux%u, layers %u, orientation %s, position %s\n", sp.systemName,
           sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight,
           sp.graphicsProperties.maxLayerCount, sp.trackingProperties.orientationTracking ? "yes" : "no",
           sp.trackingProperties.positionTracking ? "yes" : "no");
    r = enum_views(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, NULL);
    if (XR_FAILED(r) || nv != 2)
        printf("  view count: %s, %u\n", res(r), nv);
    memset(views, 0, sizeof views);
    views[0].type = views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    r = enum_views(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, views);
    if (XR_SUCCEEDED(r))
        printf("  %u views, recommended %ux%u (max %ux%u), samples %u\n", nv, views[0].recommendedImageRectWidth,
               views[0].recommendedImageRectHeight, views[0].maxImageRectWidth, views[0].maxImageRectHeight,
               views[0].recommendedSwapchainSampleCount);

    memset(&req, 0, sizeof req);
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
    r = get_req(inst, sys, &req);
    if (XR_FAILED(r)) {
        printf("  xrGetVulkanGraphicsRequirements2KHR: %s\n", res(r));
        return 0;
    }
    printf("  Vulkan wanted: %u.%u.%u to %u.%u.%u\n", XR_VERSION_MAJOR(req.minApiVersionSupported),
           XR_VERSION_MINOR(req.minApiVersionSupported), XR_VERSION_PATCH(req.minApiVersionSupported),
           XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported),
           XR_VERSION_PATCH(req.maxApiVersionSupported));

    /* Stage 3: a Vulkan instance and device made through the runtime. */
    vk = LoadLibraryA("vulkan-1.dll");
    vkgipa = vk ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr") : NULL;
    if (!vkgipa) {
        printf("stage 3: vulkan-1.dll not available\n");
        return 0;
    }
    {
        VkApplicationInfo app;
        VkInstanceCreateInfo vci;
        XrVulkanInstanceCreateInfoKHR xi;
        VkInstance vki = VK_NULL_HANDLE;
        VkResult vr = VK_SUCCESS;
        XrVulkanGraphicsDeviceGetInfoKHR di;
        VkPhysicalDevice pd = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties props;
        PFN_vkGetPhysicalDeviceProperties gpdp;
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi;
        VkDeviceCreateInfo dci;
        XrVulkanDeviceCreateInfoKHR xd;
        VkDevice vkd = VK_NULL_HANDLE;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties gqf;
        uint32_t nq = 0, qf = 0, k;
        VkQueueFamilyProperties qp[16];
        PFN_vkDestroyInstance vkDestroyInst;
        PFN_vkDestroyDevice vkDestroyDev;

        memset(&app, 0, sizeof app);
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion = getenv("XR_VK12") ? VK_API_VERSION_1_2 : VK_API_VERSION_1_3;   /* XR_VK12=1: the version the runtime states */
        memset(&vci, 0, sizeof vci);
        vci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        vci.pApplicationInfo = &app;
        memset(&xi, 0, sizeof xi);
        xi.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
        xi.systemId = sys;
        xi.pfnGetInstanceProcAddr = vkgipa;
        xi.vulkanCreateInfo = &vci;
        r = mk_inst(inst, &xi, &vki, &vr);
        if (XR_FAILED(r) || vr != VK_SUCCESS) {
            printf("stage 3: xrCreateVulkanInstanceKHR: %s, VkResult %d\n", res(r), (int)vr);
            return 0;
        }
        printf("stage 3: Vulkan 1.3 instance made through the runtime: ok\n");
        memset(&di, 0, sizeof di);
        di.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
        di.systemId = sys;
        di.vulkanInstance = vki;
        r = get_dev(inst, &di, &pd);
        if (XR_FAILED(r) || !pd) {
            printf("  xrGetVulkanGraphicsDevice2KHR: %s\n", res(r));
            return 0;
        }
        gpdp = (PFN_vkGetPhysicalDeviceProperties)vkgipa(vki, "vkGetPhysicalDeviceProperties");
        gpdp(pd, &props);
        printf("  the runtime wants the GPU \"%s\" (Vulkan %u.%u.%u)\n", props.deviceName,
               VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
               VK_API_VERSION_PATCH(props.apiVersion));
        gqf = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkgipa(vki, "vkGetPhysicalDeviceQueueFamilyProperties");
        gqf(pd, &nq, NULL);
        if (nq > 16)
            nq = 16;
        gqf(pd, &nq, qp);
        for (k = 0; k < nq; k++)
            if (qp[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                qf = k;
                break;
            }
        memset(&qi, 0, sizeof qi);
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = qf;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        memset(&dci, 0, sizeof dci);
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qi;
        memset(&xd, 0, sizeof xd);
        xd.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
        xd.systemId = sys;
        xd.pfnGetInstanceProcAddr = vkgipa;
        xd.vulkanPhysicalDevice = pd;
        xd.vulkanCreateInfo = &dci;
        r = mk_dev(inst, &xd, &vkd, &vr);
        if (XR_FAILED(r) || vr != VK_SUCCESS)
            printf("  xrCreateVulkanDeviceKHR: %s, VkResult %d\n", res(r), (int)vr);
        else
            printf("  a Vulkan device (graphics queue family %u) made through the runtime: ok\n", qf);
        if (swapchain && vkd)
            stage4(sys, vki, pd, vkd, qf, 0);
        vkDestroyDev = vkd ? (PFN_vkDestroyDevice)vkgipa(vki, "vkDestroyDevice") : NULL;
        if (vkDestroyDev)
            vkDestroyDev(vkd, NULL);
        vkDestroyInst = (PFN_vkDestroyInstance)vkgipa(vki, "vkDestroyInstance");
        vkDestroyInst(vki, NULL);
    }
    return 1;
}

int main(int argc, char **argv)
{
    HMODULE xr;
    PFN_xrEnumerateInstanceExtensionProperties enum_ext;
    int has_vk2 = 0, want_system = argc > 1 && !strcmp(argv[1], "--system");
    const char *paths[] = { "openxr_loader.dll",
                            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll" };
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0, xr = NULL; i < 2 && !xr; i++) {
        xr = LoadLibraryA(paths[i]);
        if (xr)
            printf("OpenXR loader: %s\n", paths[i]);
    }
    if (!xr) {
        printf("No openxr_loader.dll found.\n");
        return 1;
    }
    p_getproc = (PFN_xrGetInstanceProcAddr)GetProcAddress(xr, "xrGetInstanceProcAddr");
    enum_ext = (PFN_xrEnumerateInstanceExtensionProperties)GetProcAddress(xr, "xrEnumerateInstanceExtensionProperties");
    if (!p_getproc || !enum_ext) {
        printf("The loader lacks xrGetInstanceProcAddr.\n");
        return 1;
    }
    if (!stage1(enum_ext, &has_vk2))
        return 1;
    if (want_system && getenv("XR_D3D11"))
        return stage_d3d11() ? 0 : 1;
    if (want_system && getenv("XR_V1"))
        return stage_v1() ? 0 : 1;
    if (want_system) {
        if (!has_vk2) {
            printf("No XR_KHR_vulkan_enable2: stage 2 and 3 skipped.\n");
            return 1;
        }
        return stage23(argc > 2 && !strcmp(argv[2], "--swapchain")) ? 0 : 1;
    }
    printf("\n(run with --system to create an instance and Vulkan device through the runtime; it starts SteamVR)\n");
    return 0;
}

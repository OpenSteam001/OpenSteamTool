// dwmapi.dll HiJack Project

#include <windows.h>
#include <cstring>


#pragma comment(linker, "/EXPORT:DllCanUnloadNow=DWMAPI.DllCanUnloadNow,@111")
#pragma comment(linker, "/EXPORT:DllGetClassObject=DWMAPI.DllGetClassObject,@115")
#pragma comment(linker, "/EXPORT:DwmAttachMilContent=DWMAPI.DwmAttachMilContent,@116")
#pragma comment(linker, "/EXPORT:DwmDefWindowProc=DWMAPI.DwmDefWindowProc,@117")
#pragma comment(linker, "/EXPORT:DwmDetachMilContent=DWMAPI.DwmDetachMilContent,@118")
#pragma comment(linker, "/EXPORT:DwmEnableBlurBehindWindow=DWMAPI.DwmEnableBlurBehindWindow,@119")
#pragma comment(linker, "/EXPORT:DwmEnableComposition=DWMAPI.DwmEnableComposition,@102")
#pragma comment(linker, "/EXPORT:DwmEnableMMCSS=DWMAPI.DwmEnableMMCSS,@120")
#pragma comment(linker, "/EXPORT:DwmExtendFrameIntoClientArea=DWMAPI.DwmExtendFrameIntoClientArea,@121")
#pragma comment(linker, "/EXPORT:DwmFlush=DWMAPI.DwmFlush,@122")
#pragma comment(linker, "/EXPORT:DwmGetColorizationColor=DWMAPI.DwmGetColorizationColor,@123")
#pragma comment(linker, "/EXPORT:DwmGetCompositionTimingInfo=DWMAPI.DwmGetCompositionTimingInfo,@125")
#pragma comment(linker, "/EXPORT:DwmGetGraphicsStreamClient=DWMAPI.DwmGetGraphicsStreamClient,@126")
#pragma comment(linker, "/EXPORT:DwmGetGraphicsStreamTransformHint=DWMAPI.DwmGetGraphicsStreamTransformHint,@129")
#pragma comment(linker, "/EXPORT:DwmGetTransportAttributes=DWMAPI.DwmGetTransportAttributes,@130")
#pragma comment(linker, "/EXPORT:DwmGetUnmetTabRequirements=DWMAPI.DwmGetUnmetTabRequirements,@133")
#pragma comment(linker, "/EXPORT:DwmGetWindowAttribute=DWMAPI.DwmGetWindowAttribute,@134")
#pragma comment(linker, "/EXPORT:DwmInvalidateIconicBitmaps=DWMAPI.DwmInvalidateIconicBitmaps,@149")
#pragma comment(linker, "/EXPORT:DwmIsCompositionEnabled=DWMAPI.DwmIsCompositionEnabled,@188")
#pragma comment(linker, "/EXPORT:DwmModifyPreviousDxFrameDuration=DWMAPI.DwmModifyPreviousDxFrameDuration,@189")
#pragma comment(linker, "/EXPORT:DwmQueryThumbnailSourceSize=DWMAPI.DwmQueryThumbnailSourceSize,@190")
#pragma comment(linker, "/EXPORT:DwmRegisterThumbnail=DWMAPI.DwmRegisterThumbnail,@191")
#pragma comment(linker, "/EXPORT:DwmRenderGesture=DWMAPI.DwmRenderGesture,@192")
#pragma comment(linker, "/EXPORT:DwmSetDxFrameDuration=DWMAPI.DwmSetDxFrameDuration,@193")
#pragma comment(linker, "/EXPORT:DwmSetIconicLivePreviewBitmap=DWMAPI.DwmSetIconicLivePreviewBitmap,@194")
#pragma comment(linker, "/EXPORT:DwmSetIconicThumbnail=DWMAPI.DwmSetIconicThumbnail,@195")
#pragma comment(linker, "/EXPORT:DwmSetPresentParameters=DWMAPI.DwmSetPresentParameters,@196")
#pragma comment(linker, "/EXPORT:DwmSetWindowAttribute=DWMAPI.DwmSetWindowAttribute,@197")
#pragma comment(linker, "/EXPORT:DwmShowContact=DWMAPI.DwmShowContact,@198")
#pragma comment(linker, "/EXPORT:DwmTetherContact=DWMAPI.DwmTetherContact,@199")
#pragma comment(linker, "/EXPORT:DwmTetherTextContact=DWMAPI.DwmTetherTextContact,@156")
#pragma comment(linker, "/EXPORT:DwmTransitionOwnedWindow=DWMAPI.DwmTransitionOwnedWindow,@200")
#pragma comment(linker, "/EXPORT:DwmUnregisterThumbnail=DWMAPI.DwmUnregisterThumbnail,@201")
#pragma comment(linker, "/EXPORT:DwmUpdateThumbnailProperties=DWMAPI.DwmUpdateThumbnailProperties,@202")
#pragma comment(linker, "/EXPORT:DwmpAllocateSecurityDescriptor=DWMAPI.DwmpAllocateSecurityDescriptor,@136")
#pragma comment(linker, "/EXPORT:DwmpDxGetWindowSharedSurface=DWMAPI.DwmpDxGetWindowSharedSurface,@100")
#pragma comment(linker, "/EXPORT:DwmpDxUpdateWindowSharedSurface=DWMAPI.DwmpDxUpdateWindowSharedSurface,@101")
#pragma comment(linker, "/EXPORT:DwmpDxgiIsThreadDesktopComposited=DWMAPI.DwmpDxgiIsThreadDesktopComposited,@128")
#pragma comment(linker, "/EXPORT:DwmpEnableDDASupport=DWMAPI.DwmpEnableDDASupport,@143")
#pragma comment(linker, "/EXPORT:DwmpFreeSecurityDescriptor=DWMAPI.DwmpFreeSecurityDescriptor,@137")
#pragma comment(linker, "/EXPORT:DwmpGetColorizationParameters=DWMAPI.DwmpGetColorizationParameters,@127")
#pragma comment(linker, "/EXPORT:DwmpRenderFlick=DWMAPI.DwmpRenderFlick,@135")
#pragma comment(linker, "/EXPORT:DwmpSetColorizationParameters=DWMAPI.DwmpSetColorizationParameters,@131")
#pragma comment(linker, "/EXPORT:DwmpUpdateProxyWindowForCapture=DWMAPI.DwmpUpdateProxyWindowForCapture,@183")
#pragma comment(linker, "/EXPORT:#103=DWMAPI.#103,@103,NONAME")
#pragma comment(linker, "/EXPORT:#104=DWMAPI.#104,@104,NONAME")
#pragma comment(linker, "/EXPORT:#105=DWMAPI.#105,@105,NONAME")
#pragma comment(linker, "/EXPORT:#106=DWMAPI.#106,@106,NONAME")
#pragma comment(linker, "/EXPORT:#107=DWMAPI.#107,@107,NONAME")
#pragma comment(linker, "/EXPORT:#108=DWMAPI.#108,@108,NONAME")
#pragma comment(linker, "/EXPORT:#109=DWMAPI.#109,@109,NONAME")
#pragma comment(linker, "/EXPORT:#110=DWMAPI.#110,@110,NONAME")
#pragma comment(linker, "/EXPORT:#112=DWMAPI.#112,@112,NONAME")
#pragma comment(linker, "/EXPORT:#113=DWMAPI.#113,@113,NONAME")
#pragma comment(linker, "/EXPORT:#114=DWMAPI.#114,@114,NONAME")
#pragma comment(linker, "/EXPORT:#124=DWMAPI.#124,@124,NONAME")
#pragma comment(linker, "/EXPORT:#132=DWMAPI.#132,@132,NONAME")
#pragma comment(linker, "/EXPORT:#138=DWMAPI.#138,@138,NONAME")
#pragma comment(linker, "/EXPORT:#139=DWMAPI.#139,@139,NONAME")
#pragma comment(linker, "/EXPORT:#140=DWMAPI.#140,@140,NONAME")
#pragma comment(linker, "/EXPORT:#141=DWMAPI.#141,@141,NONAME")
#pragma comment(linker, "/EXPORT:#142=DWMAPI.#142,@142,NONAME")
#pragma comment(linker, "/EXPORT:#144=DWMAPI.#144,@144,NONAME")
#pragma comment(linker, "/EXPORT:#145=DWMAPI.#145,@145,NONAME")
#pragma comment(linker, "/EXPORT:#146=DWMAPI.#146,@146,NONAME")
#pragma comment(linker, "/EXPORT:#147=DWMAPI.#147,@147,NONAME")
#pragma comment(linker, "/EXPORT:#148=DWMAPI.#148,@148,NONAME")
#pragma comment(linker, "/EXPORT:#150=DWMAPI.#150,@150,NONAME")
#pragma comment(linker, "/EXPORT:#151=DWMAPI.#151,@151,NONAME")
#pragma comment(linker, "/EXPORT:#152=DWMAPI.#152,@152,NONAME")
#pragma comment(linker, "/EXPORT:#153=DWMAPI.#153,@153,NONAME")
#pragma comment(linker, "/EXPORT:#154=DWMAPI.#154,@154,NONAME")
#pragma comment(linker, "/EXPORT:#155=DWMAPI.#155,@155,NONAME")
#pragma comment(linker, "/EXPORT:#157=DWMAPI.#157,@157,NONAME")
#pragma comment(linker, "/EXPORT:#158=DWMAPI.#158,@158,NONAME")
#pragma comment(linker, "/EXPORT:#159=DWMAPI.#159,@159,NONAME")
#pragma comment(linker, "/EXPORT:#160=DWMAPI.#160,@160,NONAME")
#pragma comment(linker, "/EXPORT:#161=DWMAPI.#161,@161,NONAME")
#pragma comment(linker, "/EXPORT:#162=DWMAPI.#162,@162,NONAME")
#pragma comment(linker, "/EXPORT:#163=DWMAPI.#163,@163,NONAME")
#pragma comment(linker, "/EXPORT:#164=DWMAPI.#164,@164,NONAME")
#pragma comment(linker, "/EXPORT:#165=DWMAPI.#165,@165,NONAME")
#pragma comment(linker, "/EXPORT:#166=DWMAPI.#166,@166,NONAME")
#pragma comment(linker, "/EXPORT:#167=DWMAPI.#167,@167,NONAME")
#pragma comment(linker, "/EXPORT:#168=DWMAPI.#168,@168,NONAME")
#pragma comment(linker, "/EXPORT:#169=DWMAPI.#169,@169,NONAME")
#pragma comment(linker, "/EXPORT:#170=DWMAPI.#170,@170,NONAME")
#pragma comment(linker, "/EXPORT:#171=DWMAPI.#171,@171,NONAME")
#pragma comment(linker, "/EXPORT:#172=DWMAPI.#172,@172,NONAME")
#pragma comment(linker, "/EXPORT:#173=DWMAPI.#173,@173,NONAME")
#pragma comment(linker, "/EXPORT:#174=DWMAPI.#174,@174,NONAME")
#pragma comment(linker, "/EXPORT:#175=DWMAPI.#175,@175,NONAME")
#pragma comment(linker, "/EXPORT:#176=DWMAPI.#176,@176,NONAME")
#pragma comment(linker, "/EXPORT:#177=DWMAPI.#177,@177,NONAME")
#pragma comment(linker, "/EXPORT:#178=DWMAPI.#178,@178,NONAME")
#pragma comment(linker, "/EXPORT:#179=DWMAPI.#179,@179,NONAME")
#pragma comment(linker, "/EXPORT:#180=DWMAPI.#180,@180,NONAME")
#pragma comment(linker, "/EXPORT:#181=DWMAPI.#181,@181,NONAME")
#pragma comment(linker, "/EXPORT:#182=DWMAPI.#182,@182,NONAME")
#pragma comment(linker, "/EXPORT:#184=DWMAPI.#184,@184,NONAME")
#pragma comment(linker, "/EXPORT:#185=DWMAPI.#185,@185,NONAME")
#pragma comment(linker, "/EXPORT:#186=DWMAPI.#186,@186,NONAME")
#pragma comment(linker, "/EXPORT:#187=DWMAPI.#187,@187,NONAME")

// Only inject when the host process is steam.exe (case-insensitive).
// LoadLibraryA itself guarantees that OpenSteamTool.dll's DllMain
// runs at most once per process, so multiple hijack DLLs can safely
// call this without additional synchronisation.
BOOL OpenSteamToolLoad()
{
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH))
    {
        const char* exeName = strrchr(exePath, '\\');
        exeName = exeName ? exeName + 1 : exePath;
        if (_stricmp(exeName, "steam.exe") != 0)
            return TRUE;   // not Steam — let the proxy load, but don't inject
    }
    return LoadLibraryA("OpenSteamTool.dll") != NULL;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
            if ( !OpenSteamToolLoad() )
                return FALSE;
            break;
        }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


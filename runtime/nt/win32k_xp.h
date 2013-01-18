#ifndef WIN32K_H
#define WIN32K_H

#define DestroyPhysicalMonitor 
#define DxEngGetRedirectionBitmap 
#define DxgStubContextCreate 
#define DxgStubCreateSurfaceObject 
#define DxgStubDeleteDirectDrawObject 
#define DxgStubEnableDirectDrawRedirection 
#define GreFlush 
#define GreSelectBitmap 0x1101
#define IsIMMEnabledSystem 
#define NtGdiAbortDoc 0x1000
#define NtGdiAbortPath 0x1001
#define NtGdiAddEmbFontToDC 0x10d6
#define NtGdiAddFontMemResourceEx 0x1004
#define NtGdiAddFontResourceW 0x1002
#define NtGdiAddRemoteFontToDC 0x1003
#define NtGdiAddRemoteMMInstanceToDC 0x1006
#define NtGdiAlphaBlend 0x1007
#define NtGdiAngleArc 0x1008
#define NtGdiAnyLinkedFonts 0x1009
#define NtGdiArcInternal 0x100b
#define NtGdiBRUSHOBJ_DeleteRbrush 0x1298
#define NtGdiBRUSHOBJ_hGetColorTransform 0x127d
#define NtGdiBRUSHOBJ_pvAllocRbrush 0x127b
#define NtGdiBRUSHOBJ_pvGetRbrush 0x127c
#define NtGdiBRUSHOBJ_ulGetBrushColor 0x127a
#define NtGdiBeginGdiRendering 
#define NtGdiBeginPath 0x100c
#define NtGdiBitBlt 0x100d
#define NtGdiCLIPOBJ_bEnum 0x1274
#define NtGdiCLIPOBJ_cEnumStart 0x1275
#define NtGdiCLIPOBJ_ppoGetPath 0x1276
#define NtGdiCancelDC 0x100e
#define NtGdiChangeGhostFont 0x10d5
#define NtGdiCheckBitmapBits 0x100f
#define NtGdiClearBitmapAttributes 0x1011
#define NtGdiClearBrushAttributes 0x1012
#define NtGdiCloseFigure 0x1010
#define NtGdiColorCorrectPalette 0x1013
#define NtGdiCombineRgn 0x1014
#define NtGdiCombineTransform 0x1015
#define NtGdiComputeXformCoefficients 0x1016
#define NtGdiConfigureOPMProtectedOutput 
#define NtGdiConsoleTextOut 0x1017
#define NtGdiConvertMetafileRect 0x1018
#define NtGdiCreateBitmap 0x1019
#define NtGdiCreateBitmapFromDxSurface 
#define NtGdiCreateClientObj 0x101a
#define NtGdiCreateColorSpace 0x101b
#define NtGdiCreateColorTransform 0x101c
#define NtGdiCreateCompatibleBitmap 0x101d
#define NtGdiCreateCompatibleDC 0x101e
#define NtGdiCreateDIBBrush 0x101f
#define NtGdiCreateDIBSection 0x1021
#define NtGdiCreateDIBitmapInternal 0x1020
#define NtGdiCreateEllipticRgn 0x1022
#define NtGdiCreateHalftonePalette 0x1023
#define NtGdiCreateHatchBrushInternal 0x1024
#define NtGdiCreateMetafileDC 0x1025
#define NtGdiCreateOPMProtectedOutputs 
#define NtGdiCreatePaletteInternal 0x1026
#define NtGdiCreatePatternBrushInternal 0x1027
#define NtGdiCreatePen 0x1028
#define NtGdiCreateRectRgn 0x1029
#define NtGdiCreateRoundRectRgn 0x102a
#define NtGdiCreateServerMetaFile 0x102b
#define NtGdiCreateSolidBrush 0x102c
#define NtGdiD3dContextCreate 0x102d
#define NtGdiD3dContextDestroy 0x102e
#define NtGdiD3dContextDestroyAll 0x102f
#define NtGdiD3dDrawPrimitives2 0x1031
#define NtGdiD3dExecute 
#define NtGdiD3dExecuteClipped 
#define NtGdiD3dGetState 
#define NtGdiD3dLightSet 
#define NtGdiD3dMaterialCreate 
#define NtGdiD3dMaterialDestroy 
#define NtGdiD3dMaterialGetData 
#define NtGdiD3dMaterialSetData 
#define NtGdiD3dMatrixCreate 
#define NtGdiD3dMatrixDestroy 
#define NtGdiD3dMatrixGetData 
#define NtGdiD3dMatrixSetData 
#define NtGdiD3dRenderPrimitive 
#define NtGdiD3dRenderState 
#define NtGdiD3dSceneCapture 
#define NtGdiD3dSetViewportData 
#define NtGdiD3dTextureCreate 
#define NtGdiD3dTextureDestroy 
#define NtGdiD3dTextureGetSurf 
#define NtGdiD3dTextureSwap 
#define NtGdiD3dValidateTextureStageState 0x1030
#define NtGdiDDCCIGetCapabilitiesString 
#define NtGdiDDCCIGetCapabilitiesStringLength 
#define NtGdiDDCCIGetTimingReport 
#define NtGdiDDCCIGetVCPFeature 
#define NtGdiDDCCISaveCurrentSettings 
#define NtGdiDDCCISetVCPFeature 
#define NtGdiDdAddAttachedSurface 0x1033
#define NtGdiDdAlphaBlt 0x1034
#define NtGdiDdAttachSurface 0x1035
#define NtGdiDdBeginMoCompFrame 0x1036
#define NtGdiDdBlt 0x1037
#define NtGdiDdCanCreateD3DBuffer 0x1039
#define NtGdiDdCanCreateSurface 0x1038
#define NtGdiDdColorControl 0x103a
#define NtGdiDdCreateD3DBuffer 0x103d
#define NtGdiDdCreateDirectDrawObject 0x103b
#define NtGdiDdCreateFullscreenSprite 
#define NtGdiDdCreateMoComp 0x103e
#define NtGdiDdCreateSurface 0x103c
#define NtGdiDdCreateSurfaceEx 0x105e
#define NtGdiDdCreateSurfaceObject 0x103f
#define NtGdiDdDDIAcquireKeyedMutex 
#define NtGdiDdDDICheckExclusiveOwnership 
#define NtGdiDdDDICheckMonitorPowerState 
#define NtGdiDdDDICheckOcclusion 
#define NtGdiDdDDICheckSharedResourceAccess 
#define NtGdiDdDDICheckVidPnExclusiveOwnership 
#define NtGdiDdDDICloseAdapter 
#define NtGdiDdDDIConfigureSharedResource 
#define NtGdiDdDDICreateAllocation 
#define NtGdiDdDDICreateContext 
#define NtGdiDdDDICreateDCFromMemory 
#define NtGdiDdDDICreateDevice 
#define NtGdiDdDDICreateKeyedMutex 
#define NtGdiDdDDICreateOverlay 
#define NtGdiDdDDICreateSynchronizationObject 
#define NtGdiDdDDIDestroyAllocation 
#define NtGdiDdDDIDestroyContext 
#define NtGdiDdDDIDestroyDCFromMemory 
#define NtGdiDdDDIDestroyDevice 
#define NtGdiDdDDIDestroyKeyedMutex 
#define NtGdiDdDDIDestroyOverlay 
#define NtGdiDdDDIDestroySynchronizationObject 
#define NtGdiDdDDIEscape 
#define NtGdiDdDDIFlipOverlay 
#define NtGdiDdDDIGetContextSchedulingPriority 
#define NtGdiDdDDIGetDeviceState 
#define NtGdiDdDDIGetDisplayModeList 
#define NtGdiDdDDIGetMultisampleMethodList 
#define NtGdiDdDDIGetOverlayState 
#define NtGdiDdDDIGetPresentHistory 
#define NtGdiDdDDIGetPresentQueueEvent 
#define NtGdiDdDDIGetProcessSchedulingPriorityClass 
#define NtGdiDdDDIGetRuntimeData 
#define NtGdiDdDDIGetScanLine 
#define NtGdiDdDDIGetSharedPrimaryHandle 
#define NtGdiDdDDIInvalidateActiveVidPn 
#define NtGdiDdDDILock 
#define NtGdiDdDDIOpenAdapterFromDeviceName 
#define NtGdiDdDDIOpenAdapterFromHdc 
#define NtGdiDdDDIOpenKeyedMutex 
#define NtGdiDdDDIOpenResource 
#define NtGdiDdDDIOpenSynchronizationObject 
#define NtGdiDdDDIPollDisplayChildren 
#define NtGdiDdDDIPresent 
#define NtGdiDdDDIQueryAdapterInfo 
#define NtGdiDdDDIQueryAllocationResidency 
#define NtGdiDdDDIQueryResourceInfo 
#define NtGdiDdDDIQueryStatistics 
#define NtGdiDdDDIReleaseKeyedMutex 
#define NtGdiDdDDIReleaseProcessVidPnSourceOwners 
#define NtGdiDdDDIRender 
#define NtGdiDdDDISetAllocationPriority 
#define NtGdiDdDDISetContextSchedulingPriority 
#define NtGdiDdDDISetDisplayMode 
#define NtGdiDdDDISetDisplayPrivateDriverFormat 
#define NtGdiDdDDISetGammaRamp 
#define NtGdiDdDDISetProcessSchedulingPriorityClass 
#define NtGdiDdDDISetQueuedLimit 
#define NtGdiDdDDISetVidPnSourceOwner 
#define NtGdiDdDDISharedPrimaryLockNotification 
#define NtGdiDdDDISharedPrimaryUnLockNotification 
#define NtGdiDdDDISignalSynchronizationObject 
#define NtGdiDdDDIUnlock 
#define NtGdiDdDDIUpdateOverlay 
#define NtGdiDdDDIWaitForIdle 
#define NtGdiDdDDIWaitForSynchronizationObject 
#define NtGdiDdDDIWaitForVerticalBlankEvent 
#define NtGdiDdDeleteDirectDrawObject 0x1040
#define NtGdiDdDeleteSurfaceObject 0x1041
#define NtGdiDdDestroyD3DBuffer 0x1044
#define NtGdiDdDestroyFullscreenSprite 
#define NtGdiDdDestroyMoComp 0x1042
#define NtGdiDdDestroySurface 0x1043
#define NtGdiDdEndMoCompFrame 0x1045
#define NtGdiDdFlip 0x1046
#define NtGdiDdFlipToGDISurface 0x1047
#define NtGdiDdGetAvailDriverMemory 0x1048
#define NtGdiDdGetBltStatus 0x1049
#define NtGdiDdGetDC 0x104a
#define NtGdiDdGetDriverInfo 0x104b
#define NtGdiDdGetDriverState 0x1032
#define NtGdiDdGetDxHandle 0x104c
#define NtGdiDdGetFlipStatus 0x104d
#define NtGdiDdGetInternalMoCompInfo 0x104e
#define NtGdiDdGetMoCompBuffInfo 0x104f
#define NtGdiDdGetMoCompFormats 0x1051
#define NtGdiDdGetMoCompGuids 0x1050
#define NtGdiDdGetScanLine 0x1052
#define NtGdiDdLock 0x1053
#define NtGdiDdLockD3D 0x1054
#define NtGdiDdNotifyFullscreenSpriteUpdate 
#define NtGdiDdQueryDirectDrawObject 0x1055
#define NtGdiDdQueryMoCompStatus 0x1056
#define NtGdiDdQueryVisRgnUniqueness 
#define NtGdiDdReenableDirectDrawObject 0x1057
#define NtGdiDdReleaseDC 0x1058
#define NtGdiDdRenderMoComp 0x1059
#define NtGdiDdResetVisrgn 0x105a
#define NtGdiDdSetColorKey 0x105b
#define NtGdiDdSetExclusiveMode 0x105c
#define NtGdiDdSetGammaRamp 0x105d
#define NtGdiDdSetOverlayPosition 0x105f
#define NtGdiDdUnattachSurface 0x1060
#define NtGdiDdUnlock 0x1061
#define NtGdiDdUnlockD3D 0x1062
#define NtGdiDdUpdateOverlay 0x1063
#define NtGdiDdWaitForVerticalBlank 0x1064
#define NtGdiDeleteClientObj 0x1077
#define NtGdiDeleteColorSpace 0x1078
#define NtGdiDeleteColorTransform 0x1079
#define NtGdiDeleteObjectApp 0x107a
#define NtGdiDescribePixelFormat 0x107b
#define NtGdiDestroyOPMProtectedOutput 
#define NtGdiDoBanding 0x107d
#define NtGdiDoPalette 0x107e
#define NtGdiDrawEscape 0x107f
#define NtGdiDrawStream 0x129a
#define NtGdiDvpAcquireNotification 0x1074
#define NtGdiDvpCanCreateVideoPort 0x1065
#define NtGdiDvpColorControl 0x1066
#define NtGdiDvpCreateVideoPort 0x1067
#define NtGdiDvpDestroyVideoPort 0x1068
#define NtGdiDvpFlipVideoPort 0x1069
#define NtGdiDvpGetVideoPortBandwidth 0x106a
#define NtGdiDvpGetVideoPortConnectInfo 0x1070
#define NtGdiDvpGetVideoPortField 0x106b
#define NtGdiDvpGetVideoPortFlipStatus 0x106c
#define NtGdiDvpGetVideoPortInputFormats 0x106d
#define NtGdiDvpGetVideoPortLine 0x106e
#define NtGdiDvpGetVideoPortOutputFormats 0x106f
#define NtGdiDvpGetVideoSignalStatus 0x1071
#define NtGdiDvpReleaseNotification 0x1075
#define NtGdiDvpUpdateVideoPort 0x1072
#define NtGdiDvpWaitForVideoPortSync 0x1073
#define NtGdiDwmGetDirtyRgn 
#define NtGdiDwmGetSurfaceData 
#define NtGdiDxgGenericThunk 0x1076
#define NtGdiEllipse 0x1080
#define NtGdiEnableEudc 0x1081
#define NtGdiEndDoc 0x1082
#define NtGdiEndGdiRendering 
#define NtGdiEndPage 0x1083
#define NtGdiEndPath 0x1084
#define NtGdiEngAlphaBlend 0x126c
#define NtGdiEngAssociateSurface 0x1257
#define NtGdiEngBitBlt 0x1263
#define NtGdiEngCheckAbort 0x1293
#define NtGdiEngComputeGlyphSet 0x125c
#define NtGdiEngCopyBits 0x125d
#define NtGdiEngCreateBitmap 0x1258
#define NtGdiEngCreateClip 0x1278
#define NtGdiEngCreateDeviceBitmap 0x125a
#define NtGdiEngCreateDeviceSurface 0x1259
#define NtGdiEngCreatePalette 0x125b
#define NtGdiEngDeleteClip 0x1279
#define NtGdiEngDeletePalette 0x125e
#define NtGdiEngDeletePath 0x1277
#define NtGdiEngDeleteSurface 0x125f
#define NtGdiEngEraseSurface 0x1260
#define NtGdiEngFillPath 0x1268
#define NtGdiEngGradientFill 0x126d
#define NtGdiEngLineTo 0x126b
#define NtGdiEngLockSurface 0x1262
#define NtGdiEngMarkBandingSurface 0x1266
#define NtGdiEngPaint 0x126a
#define NtGdiEngPlgBlt 0x1265
#define NtGdiEngStretchBlt 0x1264
#define NtGdiEngStretchBltROP 0x1270
#define NtGdiEngStrokeAndFillPath 0x1269
#define NtGdiEngStrokePath 0x1267
#define NtGdiEngTextOut 0x126f
#define NtGdiEngTransparentBlt 0x126e
#define NtGdiEngUnlockSurface 0x1261
#define NtGdiEnumFontChunk 0x1085
#define NtGdiEnumFontClose 0x1086
#define NtGdiEnumFontOpen 0x1087
#define NtGdiEnumFonts 
#define NtGdiEnumObjects 0x1088
#define NtGdiEqualRgn 0x1089
#define NtGdiEudcEnumFaceNameLinkW 
#define NtGdiEudcLoadUnloadLink 0x108a
#define NtGdiExcludeClipRect 0x108b
#define NtGdiExtCreatePen 0x108c
#define NtGdiExtCreateRegion 0x108d
#define NtGdiExtEscape 0x108e
#define NtGdiExtFloodFill 0x108f
#define NtGdiExtGetObjectW 0x1090
#define NtGdiExtSelectClipRgn 0x1091
#define NtGdiExtTextOutW 0x1092
#define NtGdiFONTOBJ_cGetAllGlyphHandles 0x1287
#define NtGdiFONTOBJ_cGetGlyphs 0x1282
#define NtGdiFONTOBJ_pQueryGlyphAttrs 0x1285
#define NtGdiFONTOBJ_pfdg 0x1284
#define NtGdiFONTOBJ_pifi 0x1283
#define NtGdiFONTOBJ_pvTrueTypeFontFile 0x1286
#define NtGdiFONTOBJ_pxoGetXform 0x1281
#define NtGdiFONTOBJ_vGetInfo 0x1280
#define NtGdiFillPath 0x1093
#define NtGdiFillRgn 0x1094
#define NtGdiFixUpHandle 
#define NtGdiFlattenPath 0x1095
#define NtGdiFlush 0x1097
#define NtGdiFlushUserBatch 0x1096
#define NtGdiFontIsLinked 0x100a
#define NtGdiForceUFIMapping 0x1098
#define NtGdiFrameRgn 0x1099
#define NtGdiFullscreenControl 0x109a
#define NtGdiGetAndSetDCDword 0x109b
#define NtGdiGetAppClipBox 0x109c
#define NtGdiGetBitmapBits 0x109d
#define NtGdiGetBitmapDimension 0x109e
#define NtGdiGetBoundsRect 0x109f
#define NtGdiGetCOPPCompatibleOPMInformation 
#define NtGdiGetCertificate 
#define NtGdiGetCertificateSize 
#define NtGdiGetCharABCWidthsW 0x10a0
#define NtGdiGetCharSet 0x10a2
#define NtGdiGetCharWidthInfo 0x10a4
#define NtGdiGetCharWidthW 0x10a3
#define NtGdiGetCharacterPlacementW 0x10a1
#define NtGdiGetColorAdjustment 0x10a5
#define NtGdiGetColorSpaceforBitmap 0x10a6
#define NtGdiGetDCDword 0x10a7
#define NtGdiGetDCObject 0x10a9
#define NtGdiGetDCPoint 0x10aa
#define NtGdiGetDCforBitmap 0x10a8
#define NtGdiGetDIBitsInternal 0x10ae
#define NtGdiGetDeviceCaps 0x10ab
#define NtGdiGetDeviceCapsAll 0x10ad
#define NtGdiGetDeviceGammaRamp 0x10ac
#define NtGdiGetDeviceWidth 0x1117
#define NtGdiGetDhpdev 0x1292
#define NtGdiGetETM 0x10af
#define NtGdiGetEmbUFI 0x10d2
#define NtGdiGetEmbedFonts 0x10d4
#define NtGdiGetEudcTimeStampEx 0x10b0
#define NtGdiGetFontData 0x10b1
#define NtGdiGetFontFileData 
#define NtGdiGetFontFileInfo 
#define NtGdiGetFontResourceInfoInternalW 0x10b2
#define NtGdiGetFontUnicodeRanges 0x10d7
#define NtGdiGetGlyphIndicesW 0x10b3
#define NtGdiGetGlyphIndicesWInternal 0x10b4
#define NtGdiGetGlyphOutline 0x10b5
#define NtGdiGetKerningPairs 0x10b6
#define NtGdiGetLinkedUFIs 0x10b7
#define NtGdiGetMiterLimit 0x10b8
#define NtGdiGetMonitorID 0x10b9
#define NtGdiGetNearestColor 0x10ba
#define NtGdiGetNearestPaletteIndex 0x10bb
#define NtGdiGetNumberOfPhysicalMonitors 
#define NtGdiGetOPMInformation 
#define NtGdiGetOPMRandomNumber 
#define NtGdiGetObjectBitmapHandle 0x10bc
#define NtGdiGetOutlineTextMetricsInternalW 0x10bd
#define NtGdiGetPath 0x10be
#define NtGdiGetPerBandInfo 0x107c
#define NtGdiGetPhysicalMonitorDescription 
#define NtGdiGetPhysicalMonitors 
#define NtGdiGetPixel 0x10bf
#define NtGdiGetRandomRgn 0x10c0
#define NtGdiGetRasterizerCaps 0x10c1
#define NtGdiGetRealizationInfo 0x10c2
#define NtGdiGetRegionData 0x10c3
#define NtGdiGetRgnBox 0x10c4
#define NtGdiGetServerMetaFileBits 0x10c5
#define NtGdiGetSpoolMessage 0x10c6
#define NtGdiGetStats 0x10c7
#define NtGdiGetStockObject 0x10c8
#define NtGdiGetStringBitmapW 0x10c9
#define NtGdiGetSuggestedOPMProtectedOutputArraySize 
#define NtGdiGetSystemPaletteUse 0x10ca
#define NtGdiGetTextCharsetInfo 0x10cb
#define NtGdiGetTextExtent 0x10cc
#define NtGdiGetTextExtentExW 0x10cd
#define NtGdiGetTextFaceW 0x10ce
#define NtGdiGetTextMetricsW 0x10cf
#define NtGdiGetTransform 0x10d0
#define NtGdiGetUFI 0x10d1
#define NtGdiGetUFIBits 
#define NtGdiGetUFIPathname 0x10d3
#define NtGdiGetWidthTable 0x10d8
#define NtGdiGradientFill 0x10d9
#define NtGdiHLSurfGetInformation 
#define NtGdiHLSurfSetInformation 
#define NtGdiHT_Get8BPPFormatPalette 0x1294
#define NtGdiHT_Get8BPPMaskPalette 0x1295
#define NtGdiHfontCreate 0x10da
#define NtGdiIcmBrushInfo 0x10db
#define NtGdiInit 0x10dc
#define NtGdiInitSpool 0x10dd
#define NtGdiIntersectClipRect 0x10de
#define NtGdiInvertRgn 0x10df
#define NtGdiLineTo 0x10e0
#define NtGdiMakeFontDir 0x10e1
#define NtGdiMakeInfoDC 0x10e2
#define NtGdiMakeObjectXferable 
#define NtGdiMaskBlt 0x10e3
#define NtGdiMirrorWindowOrg 0x1118
#define NtGdiModifyWorldTransform 0x10e4
#define NtGdiMonoBitmap 0x10e5
#define NtGdiMoveTo 0x10e6
#define NtGdiOffsetClipRgn 0x10e7
#define NtGdiOffsetRgn 0x10e8
#define NtGdiOpenDCW 0x10e9
#define NtGdiPATHOBJ_bEnum 0x128e
#define NtGdiPATHOBJ_bEnumClipLines 0x1291
#define NtGdiPATHOBJ_vEnumStart 0x128f
#define NtGdiPATHOBJ_vEnumStartClipLines 0x1290
#define NtGdiPATHOBJ_vGetBounds 0x128d
#define NtGdiPatBlt 0x10ea
#define NtGdiPathToRegion 0x10ec
#define NtGdiPerf 
#define NtGdiPlgBlt 0x10ed
#define NtGdiPolyDraw 0x10ee
#define NtGdiPolyPatBlt 0x10eb
#define NtGdiPolyPolyDraw 0x10ef
#define NtGdiPolyTextOutW 0x10f0
#define NtGdiPtInRegion 0x10f1
#define NtGdiPtVisible 0x10f2
#define NtGdiQueryFontAssocInfo 0x10f4
#define NtGdiQueryFonts 0x10f3
#define NtGdiRectInRegion 0x10f6
#define NtGdiRectVisible 0x10f7
#define NtGdiRectangle 0x10f5
#define NtGdiRemoveFontMemResourceEx 0x10f9
#define NtGdiRemoveFontResourceW 0x10f8
#define NtGdiRemoveMergeFont 0x1005
#define NtGdiResetDC 0x10fa
#define NtGdiResizePalette 0x10fb
#define NtGdiRestoreDC 0x10fc
#define NtGdiRoundRect 0x10fd
#define NtGdiSTROBJ_bEnum 0x1288
#define NtGdiSTROBJ_bEnumPositionsOnly 0x1289
#define NtGdiSTROBJ_bGetAdvanceWidths 0x128a
#define NtGdiSTROBJ_dwGetCodePage 0x128c
#define NtGdiSTROBJ_vEnumStart 0x128b
#define NtGdiSaveDC 0x10fe
#define NtGdiScaleViewportExtEx 0x10ff
#define NtGdiScaleWindowExtEx 0x1100
#define NtGdiSelectBitmap 
#define NtGdiSelectBrush 0x1102
#define NtGdiSelectClipPath 0x1103
#define NtGdiSelectFont 0x1104
#define NtGdiSelectPalette 
#define NtGdiSelectPen 0x1105
#define NtGdiSetBitmapAttributes 0x1106
#define NtGdiSetBitmapBits 0x1107
#define NtGdiSetBitmapDimension 0x1108
#define NtGdiSetBoundsRect 0x1109
#define NtGdiSetBrushAttributes 0x110a
#define NtGdiSetBrushOrg 0x110b
#define NtGdiSetColorAdjustment 0x110c
#define NtGdiSetColorSpace 0x110d
#define NtGdiSetDIBitsToDeviceInternal 0x110f
#define NtGdiSetDeviceGammaRamp 0x110e
#define NtGdiSetFontEnumeration 0x1110
#define NtGdiSetFontXform 0x1111
#define NtGdiSetIcmMode 0x1112
#define NtGdiSetLayout 0x1119
#define NtGdiSetLinkedUFIs 0x1113
#define NtGdiSetMagicColors 0x1114
#define NtGdiSetMetaRgn 0x1115
#define NtGdiSetMiterLimit 0x1116
#define NtGdiSetOPMSigningKeyAndSequenceNumbers 
#define NtGdiSetPUMPDOBJ 0x1297
#define NtGdiSetPixel 0x111a
#define NtGdiSetPixelFormat 0x111b
#define NtGdiSetRectRgn 0x111c
#define NtGdiSetSizeDevice 0x1121
#define NtGdiSetSystemPaletteUse 0x111d
#define NtGdiSetTextCharacterExtra 
#define NtGdiSetTextJustification 0x111e
#define NtGdiSetVirtualResolution 0x1120
#define NtGdiSetupPublicCFONT 0x111f
#define NtGdiSfmGetNotificationTokens 
#define NtGdiStartDoc 0x1122
#define NtGdiStartPage 0x1123
#define NtGdiStretchBlt 0x1124
#define NtGdiStretchDIBitsInternal 0x1125
#define NtGdiStrokeAndFillPath 0x1126
#define NtGdiStrokePath 0x1127
#define NtGdiSwapBuffers 0x1128
#define NtGdiTransformPoints 0x1129
#define NtGdiTransparentBlt 0x112a
#define NtGdiUnloadPrinterDriver 0x112b
#define NtGdiUnmapMemFont 0x1299
#define NtGdiUnrealizeObject 0x112d
#define NtGdiUpdateColors 0x112e
#define NtGdiUpdateTransform 0x1296
#define NtGdiWidenPath 0x112f
#define NtGdiXFORMOBJ_bApplyXform 0x127e
#define NtGdiXFORMOBJ_iGetXform 0x127f
#define NtGdiXLATEOBJ_cGetPalette 0x1271
#define NtGdiXLATEOBJ_hGetColorTransform 0x1273
#define NtGdiXLATEOBJ_iXlate 0x1272
#define NtUserActivateKeyboardLayout 0x1130
#define NtUserAddClipboardFormatListener 
#define NtUserAlterWindowStyle 0x1131
#define NtUserAssociateInputContext 0x1132
#define NtUserAttachThreadInput 0x1133
#define NtUserBeginPaint 0x1134
#define NtUserBitBltSysBmp 0x1135
#define NtUserBlockInput 0x1136
#define NtUserBreak 
#define NtUserBuildHimcList 0x1137
#define NtUserBuildHwndList 0x1138
#define NtUserBuildNameList 0x1139
#define NtUserBuildPropList 0x113a
#define NtUserCalcMenuBar 0x1236
#define NtUserCalculatePopupWindowPosition 
#define NtUserCallHwnd 0x113b
#define NtUserCallHwndLock 0x113c
#define NtUserCallHwndOpt 0x113d
#define NtUserCallHwndParam 0x113e
#define NtUserCallHwndParamLock 0x113f
#define NtUserCallMsgFilter 0x1140
#define NtUserCallNextHookEx 0x1141
#define NtUserCallNoParam 0x1142
#define NtUserCallNoParamTranslate 
#define NtUserCallOneParam 0x1143
#define NtUserCallOneParamTranslate 
#define NtUserCallTwoParam 0x1144
#define NtUserChangeClipboardChain 0x1145
#define NtUserChangeDisplaySettings 0x1146
#define NtUserChangeWindowMessageFilterEx 
#define NtUserCheckAccessForIntegrityLevel 
#define NtUserCheckDesktopByThreadId 
#define NtUserCheckImeHotKey 0x1147
#define NtUserCheckMenuItem 0x1148
#define NtUserCheckMenuRadioItem 
#define NtUserCheckWindowThreadDesktop 
#define NtUserChildWindowFromPointEx 0x1149
#define NtUserClipCursor 0x114a
#define NtUserCloseClipboard 0x114b
#define NtUserCloseDesktop 0x114c
#define NtUserCloseWindowStation 0x114d
#define NtUserConsoleControl 0x114e
#define NtUserConvertMemHandle 0x114f
#define NtUserCopyAcceleratorTable 0x1150
#define NtUserCountClipboardFormats 0x1151
#define NtUserCreateAcceleratorTable 0x1152
#define NtUserCreateCaret 0x1153
#define NtUserCreateDesktop 0x1154
#define NtUserCreateDesktopEx 
#define NtUserCreateInputContext 0x1155
#define NtUserCreateLocalMemHandle 0x1156
#define NtUserCreateWindowEx 0x1157
#define NtUserCreateWindowStation 0x1158
#define NtUserCtxDisplayIOCtl 0x1256
#define NtUserDdeGetQualityOfService 0x1159
#define NtUserDdeInitialize 0x115a
#define NtUserDdeSetQualityOfService 0x115b
#define NtUserDefSetText 0x115d
#define NtUserDeferWindowPos 0x115c
#define NtUserDeleteMenu 0x115e
#define NtUserDestroyAcceleratorTable 0x115f
#define NtUserDestroyCursor 0x1160
#define NtUserDestroyInputContext 0x1161
#define NtUserDestroyMenu 0x1162
#define NtUserDestroyWindow 0x1163
#define NtUserDisableThreadIme 0x1164
#define NtUserDispatchMessage 0x1165
#define NtUserDisplayConfigGetDeviceInfo 
#define NtUserDisplayConfigSetDeviceInfo 
#define NtUserDoSoundConnect 
#define NtUserDoSoundDisconnect 
#define NtUserDragDetect 0x1166
#define NtUserDragObject 0x1167
#define NtUserDrawAnimatedRects 0x1168
#define NtUserDrawCaption 0x1169
#define NtUserDrawCaptionTemp 0x116a
#define NtUserDrawIconEx 0x116b
#define NtUserDrawMenuBarTemp 0x116c
#define NtUserDwmGetDxRgn 
#define NtUserDwmHintDxUpdate 
#define NtUserDwmStartRedirection 
#define NtUserDwmStopRedirection 
#define NtUserECQueryInputLangChange 
#define NtUserEmptyClipboard 0x116d
#define NtUserEnableMenuItem 0x116e
#define NtUserEnableScrollBar 0x116f
#define NtUserEndDeferWindowPosEx 0x1170
#define NtUserEndMenu 0x1171
#define NtUserEndPaint 0x1172
#define NtUserEndTouchOperation 
#define NtUserEnumDisplayDevices 0x1173
#define NtUserEnumDisplayMonitors 0x1174
#define NtUserEnumDisplaySettings 0x1175
#define NtUserEvent 0x1176
#define NtUserExcludeUpdateRgn 0x1177
#define NtUserFillWindow 0x1178
#define NtUserFindExistingCursorIcon 0x1179
#define NtUserFindWindowEx 0x117a
#define NtUserFlashWindowEx 0x117b
#define NtUserFrostCrashedWindow 
#define NtUserFullscreenControl 
#define NtUserGetAltTabInfo 0x117c
#define NtUserGetAncestor 0x117d
#define NtUserGetAppImeLevel 0x117e
#define NtUserGetAsyncKeyState 0x117f
#define NtUserGetAtomName 0x1180
#define NtUserGetCPD 0x118e
#define NtUserGetCaretBlinkTime 0x1181
#define NtUserGetCaretPos 0x1182
#define NtUserGetClassInfo 0x1183
#define NtUserGetClassInfoEx 
#define NtUserGetClassName 0x1184
#define NtUserGetClipCursor 0x118a
#define NtUserGetClipboardData 0x1185
#define NtUserGetClipboardFormatName 0x1186
#define NtUserGetClipboardOwner 0x1187
#define NtUserGetClipboardSequenceNumber 0x1188
#define NtUserGetClipboardViewer 0x1189
#define NtUserGetComboBoxInfo 0x118b
#define NtUserGetControlBrush 0x118c
#define NtUserGetControlColor 0x118d
#define NtUserGetCursorFrameInfo 0x118f
#define NtUserGetCursorInfo 0x1190
#define NtUserGetDC 0x1191
#define NtUserGetDCEx 0x1192
#define NtUserGetDisplayConfigBufferSizes 
#define NtUserGetDoubleClickTime 0x1193
#define NtUserGetForegroundWindow 0x1194
#define NtUserGetGUIThreadInfo 0x1196
#define NtUserGetGestureConfig 
#define NtUserGetGestureExtArgs 
#define NtUserGetGestureInfo 
#define NtUserGetGuiResources 0x1195
#define NtUserGetIconInfo 0x1197
#define NtUserGetIconSize 0x1198
#define NtUserGetImeHotKey 0x1199
#define NtUserGetImeInfoEx 0x119a
#define NtUserGetInputEvent 
#define NtUserGetInputLocaleInfo 
#define NtUserGetInternalWindowPos 0x119b
#define NtUserGetKeyNameText 0x119f
#define NtUserGetKeyState 0x11a0
#define NtUserGetKeyboardLayoutList 0x119c
#define NtUserGetKeyboardLayoutName 0x119d
#define NtUserGetKeyboardState 0x119e
#define NtUserGetLayeredWindowAttributes 0x1244
#define NtUserGetListBoxInfo 0x11a1
#define NtUserGetListboxString 
#define NtUserGetMediaChangeEvents 
#define NtUserGetMenuBarInfo 0x11a2
#define NtUserGetMenuIndex 0x11a3
#define NtUserGetMenuItemRect 0x11a4
#define NtUserGetMessage 0x11a5
#define NtUserGetMouseMovePointsEx 0x11a6
#define NtUserGetObjectInformation 0x11a7
#define NtUserGetOpenClipboardWindow 0x11a8
#define NtUserGetPriorityClipboardFormat 0x11a9
#define NtUserGetProcessWindowStation 0x11aa
#define NtUserGetProp 
#define NtUserGetRawInputBuffer 0x11ab
#define NtUserGetRawInputData 0x11ac
#define NtUserGetRawInputDeviceInfo 0x11ad
#define NtUserGetRawInputDeviceList 0x11ae
#define NtUserGetRegisteredRawInputDevices 0x11af
#define NtUserGetScrollBarInfo 0x11b0
#define NtUserGetStats 
#define NtUserGetSystemMenu 0x11b1
#define NtUserGetThreadDesktop 0x11b2
#define NtUserGetThreadState 0x11b3
#define NtUserGetTitleBarInfo 0x11b4
#define NtUserGetTopLevelWindow 
#define NtUserGetTouchInputInfo 
#define NtUserGetUpdateRect 0x11b5
#define NtUserGetUpdateRgn 0x11b6
#define NtUserGetUpdatedClipboardFormats 
#define NtUserGetUserStartupInfoFlags 
#define NtUserGetWOWClass 0x11b9
#define NtUserGetWindowCompositionAttribute 
#define NtUserGetWindowCompositionInfo 
#define NtUserGetWindowDC 0x11b7
#define NtUserGetWindowDisplayAffinity 
#define NtUserGetWindowMinimizeRect 
#define NtUserGetWindowPlacement 0x11b8
#define NtUserGetWindowRgnEx 
#define NtUserGhostWindowFromHungWindow 
#define NtUserHardErrorControl 0x11ba
#define NtUserHideCaret 0x11bb
#define NtUserHiliteMenuItem 0x11bc
#define NtUserHungWindowFromGhostWindow 
#define NtUserHwndQueryRedirectionInfo 
#define NtUserHwndSetRedirectionInfo 
#define NtUserImpersonateDdeClientWindow 0x11bd
#define NtUserInitBrushes 
#define NtUserInitTask 0x11c0
#define NtUserInitialize 0x11be
#define NtUserInitializeClientPfnArrays 0x11bf
#define NtUserInjectGesture 
#define NtUserInternalGetWindowIcon 
#define NtUserInternalGetWindowText 0x11c1
#define NtUserInvalidateRect 0x11c2
#define NtUserInvalidateRgn 0x11c3
#define NtUserIsClipboardFormatAvailable 0x11c4
#define NtUserIsTopLevelWindow 
#define NtUserIsTouchWindow 
#define NtUserKillTimer 0x11c5
#define NtUserLoadKeyboardLayoutEx 0x11c6
#define NtUserLockWindowStation 0x11c7
#define NtUserLockWindowUpdate 0x11c8
#define NtUserLockWorkStation 0x11c9
#define NtUserLogicalToPhysicalPoint 
#define NtUserMNDragLeave 0x11ce
#define NtUserMNDragOver 0x11cf
#define NtUserMagControl 
#define NtUserMagGetContextInformation 
#define NtUserMagSetContextInformation 
#define NtUserManageGestureHandlerWindow 
#define NtUserMapVirtualKeyEx 0x11ca
#define NtUserMenuItemFromPoint 0x11cb
#define NtUserMessageCall 0x11cc
#define NtUserMinMaximize 0x11cd
#define NtUserModifyUserStartupInfoFlags 0x11d0
#define NtUserModifyWindowTouchCapability 
#define NtUserMoveWindow 0x11d1
#define NtUserNotifyIMEStatus 0x11d2
#define NtUserNotifyProcessCreate 0x11d3
#define NtUserNotifyWinEvent 0x11d4
#define NtUserOpenClipboard 0x11d5
#define NtUserOpenDesktop 0x11d6
#define NtUserOpenInputDesktop 0x11d7
#define NtUserOpenThreadDesktop 
#define NtUserOpenWindowStation 0x11d8
#define NtUserPaintDesktop 0x11d9
#define NtUserPaintMenuBar 0x1237
#define NtUserPaintMonitor 
#define NtUserPeekMessage 0x11da
#define NtUserPhysicalToLogicalPoint 
#define NtUserPlayEventSound 
#define NtUserPostMessage 0x11db
#define NtUserPostThreadMessage 0x11dc
#define NtUserPrintWindow 0x11dd
#define NtUserProcessConnect 0x11de
#define NtUserQueryDisplayConfig 
#define NtUserQueryInformationThread 0x11df
#define NtUserQueryInputContext 0x11e0
#define NtUserQuerySendMessage 0x11e1
#define NtUserQueryUserCounters 0x11e2
#define NtUserQueryWindow 0x11e3
#define NtUserRealChildWindowFromPoint 0x11e4
#define NtUserRealInternalGetMessage 0x11e5
#define NtUserRealWaitMessageEx 0x11e6
#define NtUserRedrawWindow 0x11e7
#define NtUserRegisterClassExWOW 0x11e8
#define NtUserRegisterClipboardFormat 
#define NtUserRegisterErrorReportingDialog 
#define NtUserRegisterHotKey 0x11ea
#define NtUserRegisterRawInputDevices 0x11eb
#define NtUserRegisterServicesProcess 
#define NtUserRegisterSessionPort 
#define NtUserRegisterTasklist 0x11ec
#define NtUserRegisterUserApiHook 0x11e9
#define NtUserRegisterWindowMessage 0x11ed
#define NtUserRemoteConnect 0x1252
#define NtUserRemoteRedrawRectangle 0x1253
#define NtUserRemoteRedrawScreen 0x1254
#define NtUserRemoteStopScreenUpdates 0x1255
#define NtUserRemoveClipboardFormatListener 
#define NtUserRemoveMenu 0x11ee
#define NtUserRemoveProp 0x11ef
#define NtUserResolveDesktop 0x11f0
#define NtUserResolveDesktopForWOW 0x11f1
#define NtUserSBGetParms 0x11f2
#define NtUserScrollDC 0x11f3
#define NtUserScrollWindowEx 0x11f4
#define NtUserSelectPalette 0x11f5
#define NtUserSendInput 0x11f6
#define NtUserSendMessageCallback 
#define NtUserSendNotifyMessage 
#define NtUserSendTouchInput 
#define NtUserSetActiveWindow 0x11f7
#define NtUserSetAppImeLevel 0x11f8
#define NtUserSetCapture 0x11f9
#define NtUserSetChildWindowNoActivate 
#define NtUserSetClassLong 0x11fa
#define NtUserSetClassWord 0x11fb
#define NtUserSetClipboardData 0x11fc
#define NtUserSetClipboardViewer 0x11fd
#define NtUserSetConsoleReserveKeys 0x11fe
#define NtUserSetCursor 0x11ff
#define NtUserSetCursorContents 0x1200
#define NtUserSetCursorIconData 0x1201
#define NtUserSetDbgTag 0x1202
#define NtUserSetDebugErrorLevel 
#define NtUserSetDisplayConfig 
#define NtUserSetFocus 0x1203
#define NtUserSetGestureConfig 
#define NtUserSetImeHotKey 0x1204
#define NtUserSetImeInfoEx 0x1205
#define NtUserSetImeOwnerWindow 0x1206
#define NtUserSetInformationProcess 0x1207
#define NtUserSetInformationThread 0x1208
#define NtUserSetInternalWindowPos 0x1209
#define NtUserSetKeyboardState 0x120a
#define NtUserSetLayeredWindowAttributes 0x1245
#define NtUserSetLogonNotifyWindow 0x120b
#define NtUserSetMenu 0x120c
#define NtUserSetMenuContextHelpId 0x120d
#define NtUserSetMenuDefaultItem 0x120e
#define NtUserSetMenuFlagRtoL 0x120f
#define NtUserSetMirrorRendering 
#define NtUserSetObjectInformation 0x1210
#define NtUserSetParent 0x1211
#define NtUserSetProcessDPIAware 
#define NtUserSetProcessWindowStation 0x1212
#define NtUserSetProp 0x1213
#define NtUserSetRipFlags 0x1214
#define NtUserSetScrollInfo 0x1215
#define NtUserSetShellWindowEx 0x1216
#define NtUserSetSysColors 0x1217
#define NtUserSetSystemCursor 0x1218
#define NtUserSetSystemMenu 0x1219
#define NtUserSetSystemTimer 0x121a
#define NtUserSetThreadDesktop 0x121b
#define NtUserSetThreadLayoutHandles 0x121c
#define NtUserSetThreadState 0x121d
#define NtUserSetTimer 0x121e
#define NtUserSetUserStartupInfoFlags 
#define NtUserSetWinEventHook 0x1228
#define NtUserSetWindowCompositionAttribute 
#define NtUserSetWindowDisplayAffinity 
#define NtUserSetWindowFNID 0x121f
#define NtUserSetWindowLong 0x1220
#define NtUserSetWindowPlacement 0x1221
#define NtUserSetWindowPos 0x1222
#define NtUserSetWindowRgn 0x1223
#define NtUserSetWindowRgnEx 
#define NtUserSetWindowStationUser 0x1226
#define NtUserSetWindowWord 0x1227
#define NtUserSetWindowsHookAW 0x1224
#define NtUserSetWindowsHookEx 0x1225
#define NtUserSfmDestroyLogicalSurfaceBinding 
#define NtUserSfmDxBindSwapChain 
#define NtUserSfmDxGetSwapChainStats 
#define NtUserSfmDxOpenSwapChain 
#define NtUserSfmDxQuerySwapChainBindingStatus 
#define NtUserSfmDxReleaseSwapChain 
#define NtUserSfmDxReportPendingBindingsToDwm 
#define NtUserSfmDxSetSwapChainBindingStatus 
#define NtUserSfmDxSetSwapChainStats 
#define NtUserSfmGetLogicalSurfaceBinding 
#define NtUserShowCaret 0x1229
#define NtUserShowScrollBar 0x122a
#define NtUserShowSystemCursor 
#define NtUserShowWindow 0x122b
#define NtUserShowWindowAsync 0x122c
#define NtUserSoundSentry 0x122d
#define NtUserSwitchDesktop 0x122e
#define NtUserSystemParametersInfo 0x122f
#define NtUserTestForInteractiveUser 0x1230
#define NtUserThunkedMenuInfo 0x1231
#define NtUserThunkedMenuItemInfo 0x1232
#define NtUserToUnicodeEx 0x1233
#define NtUserTrackMouseEvent 0x1234
#define NtUserTrackPopupMenuEx 0x1235
#define NtUserTranslateAccelerator 0x1238
#define NtUserTranslateMessage 0x1239
#define NtUserUnhookWinEvent 0x123b
#define NtUserUnhookWindowsHookEx 0x123a
#define NtUserUnloadKeyboardLayout 0x123c
#define NtUserUnlockWindowStation 0x123d
#define NtUserUnregisterClass 0x123e
#define NtUserUnregisterHotKey 0x1240
#define NtUserUnregisterSessionPort 
#define NtUserUnregisterUserApiHook 0x123f
#define NtUserUpdateInputContext 0x1241
#define NtUserUpdateInstance 0x1242
#define NtUserUpdateLayeredWindow 0x1243
#define NtUserUpdatePerUserSystemParameters 0x1246
#define NtUserUpdateWindowTransform 
#define NtUserUserHandleGrantAccess 0x1247
#define NtUserValidateHandleSecure 0x1248
#define NtUserValidateRect 0x1249
#define NtUserValidateTimerCallback 0x124a
#define NtUserVkKeyScanEx 0x124b
#define NtUserWOWCleanup 
#define NtUserWOWFindWindow 
#define NtUserWaitForInputIdle 0x124c
#define NtUserWaitForMsgAndEvent 0x124d
#define NtUserWaitMessage 0x124e
#define NtUserWin32PoolAllocationStats 0x124f
#define NtUserWindowFromPhysicalPoint 
#define NtUserWindowFromPoint 0x1250
#define NtUserYieldTask 0x1251
#define NtUserfnCOPYDATA 
#define NtUserfnCOPYGLOBALDATA 
#define NtUserfnDDEINIT 
#define NtUserfnDWORD 
#define NtUserfnDWORDOPTINLPMSG 
#define NtUserfnGETTEXTLENGTHS 
#define NtUserfnHkINDWORD 
#define NtUserfnHkINLPCBTACTIVATESTRUCT 
#define NtUserfnHkINLPCBTCREATESTRUCT 
#define NtUserfnHkINLPDEBUGHOOKSTRUCT 
#define NtUserfnHkINLPKBDLLHOOKSTRUCT 
#define NtUserfnHkINLPMOUSEHOOKSTRUCT 
#define NtUserfnHkINLPMSG 
#define NtUserfnHkINLPMSLLHOOKSTRUCT 
#define NtUserfnHkINLPRECT 
#define NtUserfnHkOPTINLPEVENTMSG 
#define NtUserfnINCNTOUTSTRING 
#define NtUserfnINCNTOUTSTRINGNULL 
#define NtUserfnINDEVICECHANGE 
#define NtUserfnINLPCOMPAREITEMSTRUCT 
#define NtUserfnINLPCREATESTRUCT 
#define NtUserfnINLPDELETEITEMSTRUCT 
#define NtUserfnINLPDRAWITEMSTRUCT 
#define NtUserfnINLPHELPINFOSTRUCT 
#define NtUserfnINLPHLPSTRUCT 
#define NtUserfnINLPMDICREATESTRUCT 
#define NtUserfnINLPWINDOWPOS 
#define NtUserfnINOUTDRAG 
#define NtUserfnINOUTLPMEASUREITEMSTRUCT 
#define NtUserfnINOUTLPPOINT5 
#define NtUserfnINOUTLPRECT 
#define NtUserfnINOUTLPSCROLLINFO 
#define NtUserfnINOUTLPWINDOWPOS 
#define NtUserfnINOUTNCCALCSIZE 
#define NtUserfnINOUTNEXTMENU 
#define NtUserfnINOUTSTYLECHANGE 
#define NtUserfnINPAINTCLIPBRD 
#define NtUserfnINSIZECLIPBRD 
#define NtUserfnINSTRING 
#define NtUserfnINSTRINGNULL 
#define NtUserfnOPTOUTLPDWORDOPTOUTLPDWORD 
#define NtUserfnOUTDWORDINDWORD 
#define NtUserfnOUTLPRECT 
#define NtUserfnOUTSTRING 
#define NtUserfnPOPTINLPUINT 
#define NtUserfnPOUTLPINT 
#define NtUserfnSENTDDEMSG 
#define SURFACE_bUnMap 

#endif

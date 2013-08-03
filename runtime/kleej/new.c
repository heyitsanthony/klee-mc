struct JavaObject* j3MultiCallNew(struct JavaCommonClass*, int, ...)
{
	klee_uerror("not implemented", "j3.err");
	return 0;
}

/**
 * allocation seems to go:
 *   %6 = getelementptr %JavaClass* @java_lang_String, i32 0, i32 3  
 *   %11 = call i8* @VTgcmalloc(i32 32, i8* bitcast ([97 x i32 (...)*]* @java_lang_String_VT to i8*)) #1, !dbg !0
 *   %12 = bitcast i8* %11 to %JavaObject* 
 *   %17 = bitcast i8* %24 to %JavaObject**                                                 
 *   %18 = load %JavaObject** %17, align 8
 *   %19 = load %JavaObject** %stack_object_1, align 8
 *   %20 = getelementptr %JavaObject* %19, i64 0, i32 0
 *   %21 = load volatile [0 x i32 (...)*]** %20, align 8, !dbg !4
 *   call void @JnJVM_java_lang_String__0003Cinit_0003E__Ljava_lang_String_2(%JavaObject* %19, %JavaObject* %18), !dbg !5
 */


/* this is used for allocating class stuff */
char* VTGcmalloc(int32_t n, char* vtable) { return malloc(n); }
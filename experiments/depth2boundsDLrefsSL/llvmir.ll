; ModuleID = './depth2boundsDLrefsSL/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define nonnull {} addrspace(10)* @japi1_foo_253({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** %1, i32 %2) local_unnamed_addr #0 !dbg !5 {
top:
  %3 = alloca {} addrspace(10)**, align 8
  store volatile {} addrspace(10)** %1, {} addrspace(10)*** %3, align 8
  %4 = tail call {}*** @julia.get_pgcstack()
  %5 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !7, !align !8
  %6 = bitcast {} addrspace(10)* %5 to {} addrspace(10)* addrspace(10)*, !dbg !9
  %7 = addrspacecast {} addrspace(10)* addrspace(10)* %6 to {} addrspace(10)* addrspace(11)*, !dbg !9
  %8 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 3, !dbg !9
  %9 = bitcast {} addrspace(10)* addrspace(11)* %8 to i64 addrspace(11)*, !dbg !9
  %10 = load i64, i64 addrspace(11)* %9, align 8, !dbg !9, !tbaa !13, !range !17, !invariant.load !4
  %11 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(11)* %7, i64 4, !dbg !9
  %12 = bitcast {} addrspace(10)* addrspace(11)* %11 to i64 addrspace(11)*, !dbg !9
  %13 = load i64, i64 addrspace(11)* %12, align 8, !dbg !9, !tbaa !13, !range !17, !invariant.load !4
  %14 = tail call i64 @llvm.umin.i64(i64 %13, i64 %10), !dbg !18
  %15 = tail call i64 @llvm.umax.i64(i64 %14, i64 1), !dbg !21
  %16 = icmp ult i64 %14, 2, !dbg !28
  br i1 %16, label %L37, label %L17.preheader, !dbg !12

L17.preheader:                                    ; preds = %top
  %17 = bitcast {} addrspace(10)* %5 to i64 addrspace(13)* addrspace(10)*
  %18 = addrspacecast i64 addrspace(13)* addrspace(10)* %17 to i64 addrspace(13)* addrspace(11)*
  %19 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %18, align 8, !tbaa !13, !invariant.load !4, !nonnull !4
  br label %L17, !dbg !38

L17:                                              ; preds = %L17.preheader, %L17
  %value_phi3 = phi i64 [ %31, %L17 ], [ 2, %L17.preheader ]
  %20 = add i64 %value_phi3, -1, !dbg !39
  %21 = mul i64 %20, %10, !dbg !39
  %22 = add i64 %21, %20, !dbg !39
  %23 = getelementptr inbounds i64, i64 addrspace(13)* %19, i64 %22, !dbg !39
  %24 = load i64, i64 addrspace(13)* %23, align 8, !dbg !39, !tbaa !41
  %25 = add i64 %value_phi3, -2, !dbg !39
  %26 = mul i64 %25, %10, !dbg !39
  %27 = add i64 %26, %25, !dbg !39
  %28 = getelementptr inbounds i64, i64 addrspace(13)* %19, i64 %27, !dbg !39
  %29 = load i64, i64 addrspace(13)* %28, align 8, !dbg !39, !tbaa !41
  %30 = sub i64 %24, %29, !dbg !44
  store i64 %30, i64 addrspace(13)* %23, align 8, !dbg !46, !tbaa !41
  %.not.not = icmp eq i64 %value_phi3, %15, !dbg !48
  %31 = add i64 %value_phi3, 1, !dbg !50
  br i1 %.not.not, label %L37.loopexit, label %L17, !dbg !38

L37.loopexit:                                     ; preds = %L17
  br label %L37, !dbg !51

L37:                                              ; preds = %L37.loopexit, %top
  ret {} addrspace(10)* %5, !dbg !51
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.umin.i64(i64, i64) #1

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.umax.i64(i64, i64) #1

attributes #0 = { "probe-stack"="inline-asm" "thunk" }
attributes #1 = { nocallback nofree nosync nounwind readnone speculatable willreturn }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth1boundsSNrefsSL_min/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "japi1_foo_253", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !{i64 40}
!8 = !{i64 16}
!9 = !DILocation(line: 150, scope: !10, inlinedAt: !12)
!10 = distinct !DISubprogram(name: "size;", linkageName: "size", scope: !11, file: !11, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!11 = !DIFile(filename: "array.jl", directory: ".")
!12 = !DILocation(line: 4, scope: !5)
!13 = !{!14, !14, i64 0, i64 1}
!14 = !{!"jtbaa_const", !15, i64 0}
!15 = !{!"jtbaa", !16, i64 0}
!16 = !{!"jtbaa"}
!17 = !{i64 0, i64 9223372036854775807}
!18 = !DILocation(line: 480, scope: !19, inlinedAt: !12)
!19 = distinct !DISubprogram(name: "min;", linkageName: "min", scope: !20, file: !20, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!20 = !DIFile(filename: "promotion.jl", directory: ".")
!21 = !DILocation(line: 359, scope: !22, inlinedAt: !24)
!22 = distinct !DISubprogram(name: "unitrange_last;", linkageName: "unitrange_last", scope: !23, file: !23, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!23 = !DIFile(filename: "range.jl", directory: ".")
!24 = !DILocation(line: 354, scope: !25, inlinedAt: !26)
!25 = distinct !DISubprogram(name: "UnitRange;", linkageName: "UnitRange", scope: !23, file: !23, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!26 = !DILocation(line: 5, scope: !27, inlinedAt: !12)
!27 = distinct !DISubprogram(name: "Colon;", linkageName: "Colon", scope: !23, file: !23, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!28 = !DILocation(line: 83, scope: !29, inlinedAt: !31)
!29 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !30, file: !30, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!30 = !DIFile(filename: "int.jl", directory: ".")
!31 = !DILocation(line: 378, scope: !32, inlinedAt: !34)
!32 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !33, file: !33, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DIFile(filename: "operators.jl", directory: ".")
!34 = !DILocation(line: 609, scope: !35, inlinedAt: !36)
!35 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !23, file: !23, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!36 = !DILocation(line: 833, scope: !37, inlinedAt: !12)
!37 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !23, file: !23, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!38 = !DILocation(line: 5, scope: !5)
!39 = !DILocation(line: 862, scope: !40, inlinedAt: !38)
!40 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !11, file: !11, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!41 = !{!42, !42, i64 0}
!42 = !{!"jtbaa_arraybuf", !43, i64 0}
!43 = !{!"jtbaa_data", !15, i64 0}
!44 = !DILocation(line: 86, scope: !45, inlinedAt: !38)
!45 = distinct !DISubprogram(name: "-;", linkageName: "-", scope: !30, file: !30, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!46 = !DILocation(line: 905, scope: !47, inlinedAt: !38)
!47 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !11, file: !11, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!48 = !DILocation(line: 468, scope: !49, inlinedAt: !50)
!49 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !20, file: !20, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!50 = !DILocation(line: 837, scope: !37, inlinedAt: !38)
!51 = !DILocation(line: 7, scope: !5)

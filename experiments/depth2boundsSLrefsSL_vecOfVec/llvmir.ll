; ModuleID = './depth2boundsSLrefsSL_vecOfVec/llvmir.ll'
source_filename = "foo"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define void @julia_foo_134({} addrspace(10)* nonnull align 16 dereferenceable(40) %0, i64 signext %1) local_unnamed_addr #0 !dbg !5 {
top:
  %2 = tail call {}*** @julia.get_pgcstack()
  %3 = bitcast {} addrspace(10)* %0 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !7
  %4 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %3 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !7
  %5 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %4, i64 0, i32 1, !dbg !7
  %6 = load i64, i64 addrspace(11)* %5, align 8, !dbg !7, !tbaa !11, !range !16
  %.not.not = icmp eq i64 %6, 0, !dbg !17
  br i1 %.not.not, label %L60, label %L14.preheader, !dbg !10

L14.preheader:                                    ; preds = %top
  %7 = bitcast {} addrspace(10)* %0 to {} addrspace(10)* addrspace(13)* addrspace(10)*
  %8 = addrspacecast {} addrspace(10)* addrspace(13)* addrspace(10)* %7 to {} addrspace(10)* addrspace(13)* addrspace(11)*
  %9 = load {} addrspace(10)* addrspace(13)*, {} addrspace(10)* addrspace(13)* addrspace(11)* %8, align 8
  br label %idxend, !dbg !28

L48.loopexit:                                     ; preds = %pass11
  br label %L48, !dbg !31

L48:                                              ; preds = %L48.loopexit, %pass
  %.not10 = icmp eq i64 %value_phi3, %6, !dbg !31
  %10 = add nuw nsw i64 %value_phi3, 1, !dbg !34
  br i1 %.not10, label %L60.loopexit, label %idxend, !dbg !35

L60.loopexit:                                     ; preds = %L48
  br label %L60, !dbg !35

L60:                                              ; preds = %L60.loopexit, %top
  ret void, !dbg !35

idxend:                                           ; preds = %L48, %L14.preheader
  %value_phi3 = phi i64 [ %10, %L48 ], [ 1, %L14.preheader ]
  %11 = add nsw i64 %value_phi3, -1, !dbg !28
  %12 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)* addrspace(13)* %9, i64 %11, !dbg !28
  %13 = load {} addrspace(10)*, {} addrspace(10)* addrspace(13)* %12, align 8, !dbg !28, !tbaa !36
  %.not = icmp eq {} addrspace(10)* %13, null, !dbg !28
  br i1 %.not, label %fail, label %pass, !dbg !28

fail:                                             ; preds = %idxend
  tail call void @jl_throw({} addrspace(12)* addrspacecast ({}* inttoptr (i64 140234437938688 to {}*) to {} addrspace(12)*)), !dbg !28
  unreachable, !dbg !28

pass:                                             ; preds = %idxend
  %14 = bitcast {} addrspace(10)* %13 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)*, !dbg !39
  %15 = addrspacecast { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(10)* %14 to { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)*, !dbg !39
  %16 = getelementptr inbounds { i8 addrspace(13)*, i64, i16, i16, i32 }, { i8 addrspace(13)*, i64, i16, i16, i32 } addrspace(11)* %15, i64 0, i32 1, !dbg !39
  %17 = load i64, i64 addrspace(11)* %16, align 8, !dbg !39, !tbaa !11, !range !16
  %.not7.not = icmp eq i64 %17, 0, !dbg !40
  br i1 %.not7.not, label %L48, label %pass11.preheader, !dbg !30

pass11.preheader:                                 ; preds = %pass
  %18 = bitcast {} addrspace(10)* %13 to i64 addrspace(13)* addrspace(10)*
  %19 = addrspacecast i64 addrspace(13)* addrspace(10)* %18 to i64 addrspace(13)* addrspace(11)*
  %20 = load i64 addrspace(13)*, i64 addrspace(13)* addrspace(11)* %19, align 8, !tbaa !44, !nonnull !4
  br label %pass11, !dbg !35

pass11:                                           ; preds = %pass11.preheader, %pass11
  %value_phi8 = phi i64 [ %25, %pass11 ], [ 1, %pass11.preheader ]
  %21 = add nsw i64 %value_phi8, -1, !dbg !46
  %22 = getelementptr inbounds i64, i64 addrspace(13)* %20, i64 %21, !dbg !46
  %23 = load i64, i64 addrspace(13)* %22, align 8, !dbg !46, !tbaa !47
  %24 = mul i64 %23, %1, !dbg !49
  store i64 %24, i64 addrspace(13)* %22, align 8, !dbg !51, !tbaa !47
  %.not9 = icmp eq i64 %value_phi8, %17, !dbg !31
  %25 = add nuw nsw i64 %value_phi8, 1, !dbg !34
  br i1 %.not9, label %L48.loopexit, label %pass11, !dbg !35
}

define nonnull {} addrspace(10)* @jfptr_foo_135({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = load {} addrspace(10)*, {} addrspace(10)** %1, align 8, !nonnull !4, !dereferenceable !53, !align !54
  %5 = getelementptr inbounds {} addrspace(10)*, {} addrspace(10)** %1, i64 1
  %6 = bitcast {} addrspace(10)** %5 to i64 addrspace(10)**
  %7 = load i64 addrspace(10)*, i64 addrspace(10)** %6, align 8, !nonnull !4, !dereferenceable !55, !align !55
  %8 = addrspacecast i64 addrspace(10)* %7 to i64 addrspace(11)*
  %9 = load i64, i64 addrspace(11)* %8, align 8
  tail call void @julia_foo_134({} addrspace(10)* %4, i64 signext %9) #0
  ret {} addrspace(10)* addrspacecast ({}* inttoptr (i64 140234612355080 to {}*) to {} addrspace(10)*)
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

; Function Attrs: noreturn
declare void @jl_throw({} addrspace(12)*) local_unnamed_addr #2

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }
attributes #2 = { noreturn }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/experiments/depth2boundsSLrefsSL_vecOfVec/source.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "foo", linkageName: "julia_foo_134", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 215, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "length;", linkageName: "length", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "array.jl", directory: ".")
!10 = !DILocation(line: 4, scope: !5)
!11 = !{!12, !12, i64 0}
!12 = !{!"jtbaa_arraylen", !13, i64 0}
!13 = !{!"jtbaa_array", !14, i64 0}
!14 = !{!"jtbaa", !15, i64 0}
!15 = !{!"jtbaa"}
!16 = !{i64 0, i64 9223372036854775807}
!17 = !DILocation(line: 83, scope: !18, inlinedAt: !20)
!18 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!19 = !DIFile(filename: "int.jl", directory: ".")
!20 = !DILocation(line: 378, scope: !21, inlinedAt: !23)
!21 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !22, file: !22, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!22 = !DIFile(filename: "operators.jl", directory: ".")
!23 = !DILocation(line: 609, scope: !24, inlinedAt: !26)
!24 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !25, file: !25, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!25 = !DIFile(filename: "range.jl", directory: ".")
!26 = !DILocation(line: 833, scope: !27, inlinedAt: !10)
!27 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !25, file: !25, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!28 = !DILocation(line: 861, scope: !29, inlinedAt: !30)
!29 = distinct !DISubprogram(name: "getindex;", linkageName: "getindex", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!30 = !DILocation(line: 5, scope: !5)
!31 = !DILocation(line: 468, scope: !32, inlinedAt: !34)
!32 = distinct !DISubprogram(name: "==;", linkageName: "==", scope: !33, file: !33, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!33 = !DIFile(filename: "promotion.jl", directory: ".")
!34 = !DILocation(line: 837, scope: !27, inlinedAt: !35)
!35 = !DILocation(line: 6, scope: !5)
!36 = !{!37, !37, i64 0}
!37 = !{!"jtbaa_ptrarraybuf", !38, i64 0}
!38 = !{!"jtbaa_data", !14, i64 0}
!39 = !DILocation(line: 215, scope: !8, inlinedAt: !30)
!40 = !DILocation(line: 83, scope: !18, inlinedAt: !41)
!41 = !DILocation(line: 378, scope: !21, inlinedAt: !42)
!42 = !DILocation(line: 609, scope: !24, inlinedAt: !43)
!43 = !DILocation(line: 833, scope: !27, inlinedAt: !30)
!44 = !{!45, !45, i64 0}
!45 = !{!"jtbaa_arrayptr", !13, i64 0}
!46 = !DILocation(line: 861, scope: !29, inlinedAt: !35)
!47 = !{!48, !48, i64 0}
!48 = !{!"jtbaa_arraybuf", !38, i64 0}
!49 = !DILocation(line: 88, scope: !50, inlinedAt: !35)
!50 = distinct !DISubprogram(name: "*;", linkageName: "*", scope: !19, file: !19, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!51 = !DILocation(line: 903, scope: !52, inlinedAt: !35)
!52 = distinct !DISubprogram(name: "setindex!;", linkageName: "setindex!", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!53 = !{i64 40}
!54 = !{i64 16}
!55 = !{i64 8}

; ModuleID = 'optimizedloop.ll'
source_filename = "optimizedloop"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128-ni:10:11:12:13"
target triple = "x86_64-unknown-linux-gnu"

define i64 @julia_optimizedloop_32(i64 signext %0) local_unnamed_addr #0 !dbg !5 {
top:
  %1 = tail call {}*** @julia.get_pgcstack()
  %2 = icmp slt i64 %0, 1, !dbg !7
  br i1 %2, label %L29, label %L13.preheader, !dbg !18

L13.preheader:                                    ; preds = %top
  %3 = shl nuw i64 %0, 1, !dbg !19
  %4 = add nsw i64 %0, -1, !dbg !19
  %5 = zext i64 %4 to i65, !dbg !19
  %6 = add nsw i64 %0, -2, !dbg !19
  %7 = zext i64 %6 to i65, !dbg !19
  %8 = mul i65 %5, %7, !dbg !19
  %9 = lshr i65 %8, 1, !dbg !19
  %10 = trunc i65 %9 to i64, !dbg !19
  %11 = add i64 %3, %10, !dbg !19
  %12 = add i64 %11, -1, !dbg !19
  br label %L29, !dbg !20

L29:                                              ; preds = %L13.preheader, %top
  %value_phi9 = phi i64 [ 0, %top ], [ %12, %L13.preheader ]
  ret i64 %value_phi9, !dbg !20
}

define nonnull {} addrspace(10)* @jfptr_optimizedloop_33({} addrspace(10)* nocapture readnone %0, {} addrspace(10)** nocapture readonly %1, i32 %2) local_unnamed_addr #1 {
top:
  %3 = tail call {}*** @julia.get_pgcstack()
  %4 = bitcast {} addrspace(10)** %1 to i64 addrspace(10)**
  %5 = load i64 addrspace(10)*, i64 addrspace(10)** %4, align 8, !nonnull !4, !dereferenceable !21, !align !21
  %6 = addrspacecast i64 addrspace(10)* %5 to i64 addrspace(11)*
  %7 = load i64, i64 addrspace(11)* %6, align 8
  %8 = tail call i64 @julia_optimizedloop_32(i64 signext %7) #0
  %9 = tail call nonnull {} addrspace(10)* @jl_box_int64(i64 signext %8)
  ret {} addrspace(10)* %9
}

declare {}*** @julia.get_pgcstack() local_unnamed_addr

declare nonnull {} addrspace(10)* @jl_box_int64(i64 signext) local_unnamed_addr

attributes #0 = { "probe-stack"="inline-asm" }
attributes #1 = { "probe-stack"="inline-asm" "thunk" }

!llvm.module.flags = !{!0, !1}
!llvm.dbg.cu = !{!2}

!0 = !{i32 2, !"Dwarf Version", i32 4}
!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = distinct !DICompileUnit(language: DW_LANG_Julia, file: !3, producer: "julia", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !4, nameTableKind: GNU)
!3 = !DIFile(filename: "/home/sumiya11/loops/try2/LoopModels/examples/generator.jl", directory: ".")
!4 = !{}
!5 = distinct !DISubprogram(name: "optimizedloop", linkageName: "julia_optimizedloop_32", scope: null, file: !3, line: 3, type: !6, scopeLine: 3, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!6 = !DISubroutineType(types: !4)
!7 = !DILocation(line: 83, scope: !8, inlinedAt: !10)
!8 = distinct !DISubprogram(name: "<;", linkageName: "<", scope: !9, file: !9, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!9 = !DIFile(filename: "int.jl", directory: ".")
!10 = !DILocation(line: 378, scope: !11, inlinedAt: !13)
!11 = distinct !DISubprogram(name: ">;", linkageName: ">", scope: !12, file: !12, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!12 = !DIFile(filename: "operators.jl", directory: ".")
!13 = !DILocation(line: 609, scope: !14, inlinedAt: !16)
!14 = distinct !DISubprogram(name: "isempty;", linkageName: "isempty", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!15 = !DIFile(filename: "range.jl", directory: ".")
!16 = !DILocation(line: 833, scope: !17, inlinedAt: !18)
!17 = distinct !DISubprogram(name: "iterate;", linkageName: "iterate", scope: !15, file: !15, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !4)
!18 = !DILocation(line: 5, scope: !5)
!19 = !DILocation(line: 6, scope: !5)
!20 = !DILocation(line: 8, scope: !5)
!21 = !{i64 8}

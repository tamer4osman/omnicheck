# Sprint-25 audit evidence

This directory carries the artifacts of the 2026-11-24/25 independent
audit and defect-fix wave of OmniCheck 0.3.0 on nodejs/node (d2c9ac02).

## Reports

- OmniCheck_Independent_Test_Report.docx / .pdf -- the from-scratch
  independent audit (tool vs recompute per rule, contract checks,
  determinism, the N-1..N-5 defect list)
- OmniCheck_Fix_Report.docx / .pdf -- the fix wave (N-1..N-4, OC002-R,
  N-6 in source), the closed-loop rerun (43,719 files, 90,253 findings)
  and the per-rule agreement tables: OC002 / OC010 / OC011 reconcile
  exactly (0 FP / 0 FN), OC003 FN = 0, OC006 37,959/37,960 TP

## Evidence files (evidence/)

Included here (all small, text/JSON):
- audit_results.json / audit_results_rerun.json -- per-rule comparison
  (original audit and closed-loop rerun, with FP/FN samples)
- chunk_plan_rerun.json / chunk_scan_table*.json -- the explicit chunk
  plans and per-chunk findings/parse-error tallies
- determinism_evidence.json / determinism_rerun.json /
  determinism_rebuild_check.json -- byte-identity digests of repeated
  oc check runs on three successive builds
- oc_check_lib_report.json / oc_check_lib_rerun{1,2}.json /
  oc_check_lib_human.txt -- the lib/ determinism corpus runs
- oc_check_lib_triage_report.json -- OmniDecide triage sidecar output
- oc_compat_v25_to_head.json / oc_compat_esm_breaking_demo.json --
  compat gate runs (v25-to-HEAD surface diff; controlled BREAKING case)
- oc_graph_lib_head.json / oc_graph_lib_v25.json -- import graphs
- oc_packs_dump.json -- language pack inventory
- oc007_coverage_demo.json / oc009_osv_snapshot_demo.json -- rule demos

## Not included (regenerable, ~69 MB)

tool_findings_merged.json (original scan, 82,710 findings) and
tool_merged_rerun.json (rerun, 90,253 findings) are excluded from git
to keep clones lean. Regenerate them against a nodejs/node checkout at
d2c9ac02 with the preserved toolchain (see scripts/ at the audit host
or the sprint-25 archive):

    python3 rerun_plan.py        # explicit chunk lists (43,719 files)
    sh rerun_tool.sh             # oc check per chunk, exit codes recorded
    python3 rerun_merge.py       # merge chunks -> tool_merged_rerun.json
    python3 audit_compare_v3.py  # per-rule agreement -> audit_results_rerun.json

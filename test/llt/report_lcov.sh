lcov -d ./ -c -o hdt.info --rc lcov_branch_coverage=1
lcov -r hdt.info "*/llt/*" "/usr/*"  "*/build/*" "*/test/*" "*7.3.0*" "*/3rdparty/*" -o hdt_coverage.info --rc lcov_branch_coverage=1
genhtml -o hdt_report hdt_coverage.info --rc genhtml_branch_coverage=1
"""
Used to compare two autobahn python reports for performance
"""
import json
import sys
from collections import OrderedDict
import os

if len(sys.argv) != 3:
    print "usage:", sys.argv[0], "<report1.json> <report2.json>"
    sys.exit(1)

reports = OrderedDict()
totals = []

def get_json(filename):
    with open(filename) as f:
        return json.loads(f.read())

def parse_report(report, root_path):
    global reports
    global totals
    total = 0
    for test in report['AutobahnServer'].values():
        test_json = get_json(os.path.join(root_path, test['reportfile']))
        duration = test_json['duration']
        reports.setdefault(test['reportfile'], []).append(duration)
        total += duration

    totals.append(total)

file1 = get_json(sys.argv[1])
parse_report(file1, os.path.split(sys.argv[1])[0])
file2 = get_json(sys.argv[2])
parse_report(file2, os.path.split(sys.argv[2])[0])

lines = []
for (test, durations) in reports.items():
    if durations[0] < durations[1]:
        line = "%s: \033[32m%5d\033[0m \033[31m%5d\033[0m \033[32m-%-5d (%.2fx)\033[0m" % (test, durations[0], durations[1], durations[1] - durations[0], float(durations[1]) / float(durations[0]))
    elif durations[0] > durations[1]:
        line = "%s: \033[31m%5d\033[0m \033[32m%5d\033[0m \033[31m+%-5d (%.2fx)\033[0m" % (test, durations[0], durations[1], durations[0] - durations[1], float(durations[0]) / float(durations[1]))
    else:
        line = "%s: \033[33m%5d\033[0m \033[33m%5d\033[0m ~" % (test, durations[0], durations[1])

    lines.append((abs(durations[1] - durations[0]), line))

lines.sort(cmp=lambda x,y: cmp(x[0], y[0]))
for line in lines:
    print line[1]

if totals[0] < totals[1]:
    totals_str = "%s is \033[32m%.2fx\033[0m faster than %s" % (sys.argv[1], float(totals[1]) / float(totals[0]), sys.argv[2])
    print "totals: \033[32m%d\033[0m \033[31m%d\033[0m (%s)" % (totals[0], totals[1], totals_str)
elif totals[0] > totals[1]:
    percent_str = "%s is \033[31m%.2fx\033[0m slower than %s" % (sys.argv[1], float(totals[0]) / float(totals[1]), sys.argv[2])
    print "totals: \033[31m%d\033[0m \033[32m%d\033[0m (%s)" % (totals[0], totals[1], percent_str)
else:
    print "totals: \033[33m%d\033[0m \033[33m%d\033[0m (%s)" % (totals[0], totals[1], "EQUAL1?!?!?!")


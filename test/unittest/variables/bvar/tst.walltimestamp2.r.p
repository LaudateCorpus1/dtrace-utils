#!/usr/bin/gawk -f
#
# Oracle Linux DTrace.
# Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
# Licensed under the Universal Permissive License v 1.0 as shown at
# http://oss.oracle.com/licenses/upl.

{
        # read the value generated by system("date")
        t0 = $1; getline;

        # read a walltimestamp value from DTrace
        t1 = $1; getline;

        # read a few walltimestamp deltas from DTrace
        d2 = $1; getline;
        d3 = $1; getline;
        d4 = $1; getline;
        d5 = $1;

        # check that the deltas are all positive
        if (d2 <= 0 ||
            d3 <= 0 ||
            d4 <= 0 ||
            d5 <= 0) print "ERROR: walltimestamp did not advance.";

        # check that the deltas are all under 0.2 seconds
        if (d2 > 200000000 ||
            d3 > 200000000 ||
            d4 > 200000000 ||
            d5 > 200000000) print "ERROR: walltimestamp delta is high.";

        # check walltimestamp against system("date")
        t_error = 1.e-9 * t1 - t0;
        if (t_error < 0) t_error *= -1;
        if (t_error > 0.2) print "ERROR: walltimestamp and system(date) disagree.";

        print "success";
        exit 0;
}

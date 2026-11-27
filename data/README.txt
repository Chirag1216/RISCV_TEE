data/ holds trajectories and the command files made from them.

placeholder.txt        synthetic figure-of-eight flight (tools/make_synthetic.py). REPLACE
                       with the real ORB-SLAM3 output on EuRoC, e.g.:
                         f_dataset-MH01_stereoi.txt   (calibration: MH_01 .. MH_05)
                         f_dataset-V101_stereoi.txt   (test: V1_*, V2_*)
                       Format: timestamp tx ty tz qx qy qz qw  (one pose per line)

<name>_<attack>.cmd         command stream for the replayer (tools/inject.py)
<name>_<attack>.labels.csv  ground truth for tools/score.py

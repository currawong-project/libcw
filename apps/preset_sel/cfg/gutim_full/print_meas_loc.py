import csv

def print_meas_locs( fn ):

    with open(fn) as f:
        rdr = csv.DictReader(f)

        bar = None
        oloc0 = None
        oloc1 = None
        for row in rdr:
                            
            if row['bar']:
               bar = row['bar']

            if row['oloc']:
                oloc = row['oloc']

                if bar:
                    print(f"m{bar} {oloc0} ")
                    bar = None
                

                oloc0 = oloc

if __name__ == "__main__":

    fn = "/home/kevin/src/cwtest/src/cwtest/cfg/gutim_full/data/score_scriabin/20240428/temp_with_scriabin_2.csv"

    print_meas_locs(fn)

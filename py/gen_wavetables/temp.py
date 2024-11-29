def process_all_samples_0( markL, smpM, srate, args ):

    wtL = []

    fund_hz = 13.75 * math.pow(2,(-9.0/12.0)) * math.pow(2.0,(args.midi_pitch / 12.0))

    end_offs_smp_idx = int(args.end_offset_ms * srate / 1000)
    loop_dur_smp     = int(args.loop_dur_ms   * srate / 1000)
    smp_per_cycle    = int(srate / fund_hz)

    print(f"Hz:{fund_hz} smp/cycle:{smp_per_cycle}")
    
    for beg_sec,end_sec,vel_label in markL:
        for ch_idx in range(0,smpM.shape[1]):
            beg_smp_idx = int(beg_sec * srate)
            end_smp_idx = int(end_sec * srate)

            eli = end_smp_idx - end_offs_smp_idx
            bli = eli - loop_dur_smp

            #print(beg_smp_idx,bli,eli,end_smp_idx)

            xV    = smpM[:,ch_idx]
            wndN = int(smp_per_cycle/3)
            bi,ei,cost = find_loop_points(xV,bli,eli,smp_per_cycle,wndN,args.guess_cnt)

            plot_title = f"vel:{vel_label} ch:{ch_idx} cost:{math.log(cost):.2f}"
            #plot_overlap(xV,bi,ei,wndN,smp_per_cycle,plot_title)

            wtL.append( {
                "pitch":args.midi_pitch,
                "vel": int(vel_label),
                "cost":cost,
                "ch_idx":ch_idx,
                "beg_smp_idx":beg_smp_idx,
                "end_smp_idx":end_smp_idx,
                "beg_loop_idx":bi,
                "end_loop_idx":ei })

    return wtL


def write_loop_label_file_0( fname, wtL, srate ):

    with open(fname,"w") as f:
        for r in wtL:
            beg_sec = r['beg_smp_idx'] / srate
            end_sec = r['end_smp_idx'] / srate            
            # f.write(f"{beg_sec}\t{end_sec}\t{r['vel_label']}\n")
            
            beg_sec = r['beg_loop_idx'] / srate
            end_sec = r['end_loop_idx'] / srate
            cost    = math.log(r['cost'])
            label = f"ch:{r['ch_idx']} {cost:.2f}"
            f.write(f"{beg_sec}\t{end_sec}\t{label}\n")

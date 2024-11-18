let run_test num_threads num_runs exe =
  let args = [exe] in
  let rec run_some_tests num_threads num_runs =
    if num_threads <= 1 then (
      Forkhelpers.execute_command_get_output exe args |> ignore ;
      if num_runs > 1 then run_some_tests 1 (num_runs - 1)
    ) else
      let half = num_threads / 2 in
      let th =
        Thread.create
          (fun num_threads -> run_some_tests num_threads num_runs)
          half
      in
      run_some_tests (num_threads - half) num_runs ;
      Thread.join th
  in
  run_some_tests num_threads num_runs

let _ =
  let num_threads = ref 1 in
  let num_runs = ref 1000 in
  let program = ref "/usr/bin/true" in
  let daemon = ref false in
  let usage_msg =
    "fe_test_speed [--threads <num>] [--runs <num>] [--program <name>] \
     [--daemon]"
  in
  let anon_fun _ = raise (Arg.Bad "additional argument not expected") in
  let speclist =
    [
      ("--daemon", Arg.Set daemon, "Use forkexec daemon")
    ; ("--threads", Arg.Set_int num_threads, "Number of threads, default 1")
    ; ("--runs", Arg.Set_int num_runs, "Number of runs, default 1000")
    ; ( "--program"
      , Arg.Set_string program
      , "Program to run, default /usr/bin/true"
      )
    ]
  in
  Arg.parse speclist anon_fun usage_msg ;

  Printf.printf "%d threads %d run %s program %b daemon\n" !num_threads
    !num_runs !program !daemon ;
  if !daemon then Forkhelpers.use_daemon := true ;

  let start = Unix.gettimeofday () in
  run_test !num_threads !num_runs !program ;

  let elapsed = Unix.gettimeofday () -. start in
  Printf.printf "It took %f seconds\n%!" elapsed

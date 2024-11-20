let stop_all_thread = Atomic.make false

let num_executions = Atomic.make 0

let run_test num_threads num_runs exe =
  let args = [exe] in
  let thread_runs () =
    let runned = ref 0 in
    while num_runs - !runned >= 1 && not (Atomic.get stop_all_thread) do
      Forkhelpers.execute_command_get_output exe args |> ignore ;
      runned := !runned + 1
    done ;
    Atomic.fetch_and_add num_executions !runned |> ignore
  in
  let rec run_some_tests num_threads =
    if num_threads <= 1 then
      thread_runs ()
    else
      let half = num_threads / 2 in
      let th =
        Thread.create (fun num_threads -> run_some_tests num_threads) half
      in
      run_some_tests (num_threads - half) ;
      Thread.join th
  in
  run_some_tests num_threads

let _ =
  let num_threads = ref 1 in
  let num_runs = ref 1000 in
  let program = ref "/usr/bin/true" in
  let daemon = ref false in
  let timeout = ref 0 in
  let usage_msg =
    "fe_test_speed [--threads <num>] [--runs <num>] [--timeout <seconds>] \
     [--program <name>] [--daemon]"
  in
  let anon_fun _ = raise (Arg.Bad "additional argument not expected") in
  let speclist =
    [
      ("--daemon", Arg.Set daemon, "Use forkexec daemon")
    ; ("--threads", Arg.Set_int num_threads, "Number of threads, default 1")
    ; ("--runs", Arg.Set_int num_runs, "Number of runs, default 1000")
    ; ("--timeout", Arg.Set_int timeout, "Timeout, default none")
    ; ( "--program"
      , Arg.Set_string program
      , "Program to run, default /usr/bin/true"
      )
    ]
  in
  Arg.parse speclist anon_fun usage_msg ;

  Printf.printf "%d threads %d run %d timeout %s program %b daemon\n"
    !num_threads !num_runs !timeout !program !daemon ;
  if !timeout > 0 then num_runs := max_int ;
  if !daemon then Forkhelpers.use_daemon := true ;

  let waiting_thread =
    if !timeout > 0 then
      Some
        (Thread.create
           (fun timeout ->
             Thread.delay timeout ;
             Atomic.set stop_all_thread true
           )
           (float_of_int !timeout)
        )
    else
      None
  in

  let start = Unix.gettimeofday () in
  run_test !num_threads !num_runs !program ;

  let elapsed = Unix.gettimeofday () -. start in
  let num_runned = Atomic.get num_executions in
  Printf.printf "It took %f seconds for %d runs\n%!" elapsed num_runned ;

  match waiting_thread with Some th -> Thread.join th | None -> ()

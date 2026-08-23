\ --- Fixed logic.fs script ---
: CHECK-ALARM
    dup 50 > if
        cr . cr
    else
        \ We do not drop the number here, because MONITOR-LOOP still needs it!
    then ;

: MONITOR-LOOP
    55 begin
        dup CHECK-ALARM
        1 -
        dup 45 =
    until
    drop ;
     
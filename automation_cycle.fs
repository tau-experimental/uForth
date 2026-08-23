\ --- Industrial Lifecycle & I/O Verification Script ---
\ invoke with: s" automation_cycle.fs" included
\ 1. Instantiate the Value descriptors to handle our File IDs and Heap pointers
0 value CELL-ADDR
0 value HEAP-ADDR
0 value TEST-FILE

: RUN-LIFECYCLE
    cr ." [STAGE 1]: Allocating fast hardware cell... " cr
    fast-cell -> CELL-ADDR
    
    \ Save a 32-bit tracking telemetry constant (Hex $DEADBEEF) into the cell
     %11001010 CELL-ADDR !
    ."   Cell secured at: " CELL-ADDR . cr
    ."   Cell holds data: " CELL-ADDR @ . cr

    cr ." [STAGE 2]: Creating telemetry text file... " cr
    s" telemetry_snapshot.bin" f-create -> TEST-FILE
    ."   File assigned VFS Slot: " TEST-FILE . cr

    \ Write the 4 bytes containing our data straight from the fast cell to disk
    CELL-ADDR 4 TEST-FILE f-write
    TEST-FILE f-close
    ."   Data streamed and file closed cleanly. " cr

    cr ." [STAGE 3]: Cleaning up fast cell infrastructure... " cr
    CELL-ADDR fast-cell-free
    ."   Cell cleanly returned to the atomic pool." cr

    cr ." [STAGE 4]: Performing isolated data stack arithmetic calculation... " cr
    100 200 + 5 * 1500 = if
        ."   Calculation evaluation: SUCCESS (1500 verified)" cr
    else
        ."   Calculation evaluation: FAILED" cr
     	abort
    then

    cr ." [STAGE 5]: Re-opening file for reading and allocating heap... " cr
    s" telemetry_snapshot.bin" f-open -> TEST-FILE
    
    \ Grab a 4-byte chunk out of our automated compacting SPI-RAM heap
    4 allocate drop -> HEAP-ADDR
    ."   Heap block secured at: " HEAP-ADDR . cr

    \ Package-burst extract the 4 bytes directly across our SPI bus into the heap
    HEAP-ADDR 4 TEST-FILE f-read drop
    TEST-FILE f-close
    ."   Data read back and file closed cleanly. " cr

    cr ." [STAGE 6]: FINAL INTEGRITY VERIFICATION " cr
    ."   Extracted data currently living in Heap block: " HEAP-ADDR @ . cr
    
    HEAP-ADDR @ %11001010 = if
        ."   [SUCCESS]: Hardware lifecycle execution completed flawlessly!" cr
    else
        ."   [CRITICAL FAULT]: Data corruption or bit-flip detected in transit!" cr
    then
    cr
    
    \ Return the block to the heap pool and run our automatic coalesce routine
    HEAP-ADDR free . ;
    
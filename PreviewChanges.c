int disk_read_real(int unit, int track, int first, int sectors, void *buffer)
{
    if(DEBUG4 && debugflag4)
        console("    - disk_read_real(): Entering the disk_read_real function...\n");
    if(unit > DISK_UNITS || track >= num_tracks[unit] || first >= DISK_TRACK_SIZE)
    {
        if(DEBUG4 && debugflag4)
            console("        - disk_read_real(): invalid arguments. Returning...\n");
        return -1;
    }

    check_kernel_mode("disk_read_real");

    driver_proc request;
    request.track_start = track;
    request.sector_start = first;
    request.num_sectors = sectors;
    request.disk_buf = buffer;
    request.operation = DISK_READ;
    request.next = NULL;
    request.pid = getpid();

    disk_req(&request, unit);

    semv_real(ProcTable4[diskpids[unit]].disk_sem);
    semp_real(ProcTable4[getpid()%MAXPROC].disk_sem);

    int status;
    device_input(DISK_DEV, unit, &status);

    return 0;

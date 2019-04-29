        if(Disk_QueueB[unit] == NULL)
        {
            if(DEBUG4 && debugflag4)
                console("        - disk_req(): Disk Queue is empty. Saving to bottom\n");

            Disk_QueueB[unit] = &(request);
        }
        else
        {
            if(DEBUG4 && debugflag4)
                console("        - disk_req(): Disk Queue is not empty\n");

            driver_proc_ptr curr = Disk_QueueB[unit];
            driver_proc_ptr last = NULL;

            while(curr != NULL && curr->track_start < request.track_start)
            {
                last = curr;
                curr = curr->next;
            }

            while(curr != NULL && curr->sector_start < request.sector_start)
            {
                last = curr;
                curr = curr->next;
            }

            if(last == NULL)
            {
                request.next = Disk_QueueB[unit];
                Disk_QueueB[unit] = &request;
            }
            else
            {
                last->next = &request;
                request.next = curr;
            }
        }

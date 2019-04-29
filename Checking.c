        if(Disk_QueueT[unit] == NULL)
        {
            if(DEBUG4 && debugflag4)
                console("        - disk_req(): Disk Queue is empty. Saving to top\n");
                /* Added above back in, since otherwise below is only executed while debug4 is 1 */

            Disk_QueueT[unit] = &(request);
        }
        else
        {
            //console("message2: %s\n", Disk_QueueT[unit]->disk_buf);
            if(DEBUG4 && debugflag4)
                console("        - disk_req(): Disk Queue is not empty\n");

            driver_proc_ptr curr = Disk_QueueT[unit];
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
                request.next = Disk_QueueT[unit];
                Disk_QueueT[unit] = &request;
            }
            else
            {
                last->next = &request;
                request.next = curr;
            }
        }

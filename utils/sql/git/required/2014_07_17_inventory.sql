-- Fix normal bag slots
update inventory set slotid = slotid -1 where slotid > 249 and slotid < 330 order by slotid;
-- Fix bank bag slots
update inventory set slotid = slotid -1 where slotid > 2029 and slotid < 2110 order by slotid;
-- Remove invalid items between last normal bag slot and first bank slot
delete from inventory where slotid > 339 and slotid < 2000; 
-- Remove invalid items between last bank bag slot and cursor queue slot
delete from inventory where slotid > 2109 and slotid < 8000;
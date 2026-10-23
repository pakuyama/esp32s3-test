-- XIAO ESP32-S3 remote control: run once in Supabase SQL Editor

create table if not exists public.devices (
  id          text primary key,
  led         boolean not null default false,  -- desired LED state (set by dashboard)
  touched     boolean not null default false,  -- reported by device
  touch_value integer,
  camera      boolean not null default false,  -- stream camera frames (set by dashboard)
  ip          text,                            -- LAN IP reported by device (MJPEG at http://<ip>:81/stream)
  updated_at  timestamptz not null default now()
);

insert into public.devices (id) values ('xiao-s3') on conflict (id) do nothing;

-- Server-side timestamp so the dashboard can tell if the device is online
create or replace function public.touch_updated_at() returns trigger
language plpgsql set search_path = '' as $$
begin
  new.updated_at := now();
  return new;
end $$;

drop trigger if exists devices_updated_at on public.devices;
create trigger devices_updated_at before update on public.devices
  for each row execute function public.touch_updated_at();

-- Demo policy: anyone holding the anon/publishable key can read and update rows (no insert/delete)
alter table public.devices enable row level security;
drop policy if exists "anon read"   on public.devices;
drop policy if exists "anon update" on public.devices;
create policy "anon read"   on public.devices for select to anon using (true);
create policy "anon update" on public.devices for update to anon using (true) with check (true);

-- Push changes to the dashboard in real time
do $$ begin
  alter publication supabase_realtime add table public.devices;
exception when duplicate_object then null;
end $$;

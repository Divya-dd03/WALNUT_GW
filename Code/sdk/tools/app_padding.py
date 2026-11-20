import os, sys,json

if len(sys.argv) != 4:
    print("Usage: app_padding.py [app_path] [padding_size]")

app_path = sys.argv[1]
partition_cfg = sys.argv[2]
pad_name = sys.argv[3]
if not os.path.exists(app_path):
    print("File not exist:",app_path)
    exit()

if not os.path.exists(partition_cfg):
    print("File not exist:",partition_cfg)
    exit()


pad_size = 0
fsize = os.path.getsize(app_path)
with open(partition_cfg, "r") as fd:
    part_cfg = json.loads(fd.read())
    for item in part_cfg["partitions"][0]["partitions"]:
        if item["name"].__eq__("customer_app"):
            pad_size = int(item["size"].replace("KiB", ""))*1024
if pad_size < fsize:
    print("Padding size too small")
    exit()

pad_bytes = b"\x00"*(pad_size - fsize)
with open(pad_name, "wb+") as fd:
    with open(app_path, "rb") as sfd:
        datas = fd.write(sfd.read())
    fd.write(pad_bytes)
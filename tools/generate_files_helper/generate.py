import array
import os

f = open("log_ps2.txt", "r")
lines_ps2 = f.read().split("\n")
f.close()

f = open("log_psp.txt", "r")
lines_psp = f.read().split("\n")
f.close()

f = open("log_pc.txt", "r")
lines_pc = f.read().split("\n")
f.close()

files_lower = []
files = []

for path in lines_ps2:
    path_lower = path.lower()
    if path_lower not in files_lower:
        files.append(path)
        files_lower.append(path_lower)

for path in lines_psp:
    path_lower = path.lower()
    if path_lower not in files_lower:
        files.append(path)
        files_lower.append(path_lower)

files_pc = []
libs_pc = []

for path in lines_pc:
    path_lower = path.lower()
    if path_lower.endswith(".obj"):
        libs_pc.append(path)
        continue
    if path_lower not in files_lower:
        files.append(path)
        files_lower.append(path_lower)
        files_pc.append(path)
        
files.sort()
files_pc.sort()
libs_pc.sort()

for lib in libs_pc:
    print(lib)
print("")

#for path in files:
#    os.makedirs(os.path.dirname(path), exist_ok=True)
#    open(path, 'a').close()
print(len(files_pc))
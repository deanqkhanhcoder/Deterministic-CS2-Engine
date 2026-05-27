import os
import re

root = r"c:\Users\toanpq\Desktop\marco"
dirs_to_search = [os.path.join(root, "src"), os.path.join(root, "include")]

pattern = re.compile(r'#include\s+"(\.\./)+[a-zA-Z0-9_]+/([a-zA-Z0-9_]+\.h)"')

for d in dirs_to_search:
    for root_dir, _, files in os.walk(d):
        for f in files:
            if f.endswith(".cpp") or f.endswith(".h"):
                path = os.path.join(root_dir, f)
                with open(path, "r", encoding="utf-8", errors="ignore") as file:
                    content = file.read()
                
                new_content = pattern.sub(r'#include "\2"', content)
                
                if new_content != content:
                    print(f"Fixing includes in {path}")
                    with open(path, "w", encoding="utf-8") as file:
                        file.write(new_content)

print("Done fixing includes.")

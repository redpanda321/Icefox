import os, sys, subprocess

def change_install_names(fp, app):
    p = subprocess.Popen(['otool', '-L', fp], stdout=subprocess.PIPE)
    swaps = {}
    for line in p.stdout:
        line = line.strip()
        if line.startswith("@executable_path"):
            old = line.split()[0]
            new = "/Applications/%s/%s" % (app, old.split('/')[1])
            swaps[old] = new
    p.wait()
    # terrible
    subprocess.call(["install_name_tool"] +
                    [a for old, new in swaps.iteritems() for a in ('-change',old,new)] +
                    [fp])
    return [os.path.basename(n) for n in swaps.values()]

f = sys.argv[1]
files = list([os.path.basename(f)])
appdir = os.path.abspath(os.path.dirname(f))
app = os.path.basename(appdir)
for f in files:
    print "Processing %s" % f
    fp = os.path.join(appdir, f)
    for n in change_install_names(fp, app):
        if n not in files:
            files.append(n)

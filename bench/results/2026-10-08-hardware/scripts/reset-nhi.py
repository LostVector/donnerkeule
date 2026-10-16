from control import HOSTS,LAB,both,switch
snapshot='''set -euo pipefail
umask 077
lab=/home/sn/tbv/hardware-20261007-xdomain-done
file=$(modinfo -n thunderbolt)
xz -dc "$file" | sudo -n tee "$lab/nhi-original.ko" >/dev/null
sudo -n objcopy --dump-section .note.gnu.build-id="$lab/nhi-original.note" "$lab/nhi-original.ko" "$lab/nhi-note-extraction.ko"
sudo -n cmp "$lab/nhi-original.note" /sys/module/thunderbolt/notes/.note.gnu.build-id
sudo -n rm "$lab/nhi-note-extraction.ko"
for f in /sys/module/thunderbolt/parameters/*; do printf '%s=%s\\n' "${f##*/}" "$(cat "$f")"; done > "$lab/private/nhi-original.parameters"
'''
both(dict.fromkeys(HOSTS,snapshot))
both(dict.fromkeys(HOSTS,f'sudo -n {LAB}/module.sh {LAB} unload'))
both(dict.fromkeys(HOSTS,'sudo -n rmmod thunderbolt'))
if '--pm' in __import__('sys').argv or '--acpi' in __import__('sys').argv:
 reset='''from pathlib import Path
for bdf in ('0000:c6:00.5','0000:c6:00.6'):
 p=Path('/sys/bus/pci/devices')/bdf
 methods=(p/'reset_method').read_text()
 method='METHOD'
 assert method in methods.split() and not (p/'driver').exists()
 try:
  (p/'reset_method').write_text(method)
  (p/'reset').write_text('1')
 finally:
  (p/'reset_method').write_text(methods)
 print(method,'reset of USB4 NHI',bdf)
'''
 reset=reset.replace('METHOD','acpi' if '--acpi' in __import__('sys').argv else 'pm')
 both(dict.fromkeys(HOSTS,'sudo -n python3 -c '+__import__('shlex').quote(reset)))
load='''set -euo pipefail
lab=/home/sn/tbv/hardware-20261007-xdomain-done
mapfile -t params < "$lab/private/nhi-original.parameters"
sudo -n insmod "$lab/nhi-original.ko" "${params[@]}"
sudo -n cmp "$lab/nhi-original.note" /sys/module/thunderbolt/notes/.note.gnu.build-id
'''
both(dict.fromkeys(HOSTS,'bash -c '+__import__('shlex').quote(load)))
switch('dmabuf',1,0,2)
print('NHI restarted with original binary and parameters on both hosts',flush=True)

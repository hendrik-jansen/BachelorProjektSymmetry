import os
import subprocess
import sys

if __name__ == "__main__":
  cnfs = os.listdir('./test_cnfs')

  for cnf in cnfs:
    if (cnf[-4:] != ".cnf"):
      continue
    res = subprocess.check_output([f'./{sys.argv[1]}', f'./test_cnfs/{cnf}'])
    with open(f"./test_cnfs/{cnf[:-4]}.log", 'r') as log:
      if res.decode('ascii') == log.read():
        print(f"Test on {cnf} successful!")
      else:
        print(f"Test on {cnf} failed.")

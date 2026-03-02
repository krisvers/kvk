import sys

def main():
	file = open(sys.argv[1], "rb")
	i = 0
	for c in file.read():
		if (i % 32) == 0:
			print()

		print(hex(c) + ", ", end="")
		i += 1

	file.close()

if __name__ == "__main__":
	main()
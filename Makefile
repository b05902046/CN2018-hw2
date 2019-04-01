all:
	gcc -o sender sender.c
	gcc -o receiver receiver.c
	gcc -o agent agent.c
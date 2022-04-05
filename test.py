#!/usr/bin/python3
from json import loads
from ringstat import Ring


def main():
    r = Ring('qwe')
    while True:
        #loads(r.get())
        r.get()

if __name__ == '__main__':
    main()

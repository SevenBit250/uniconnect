import os

BS = chr(92)  # backslash

def add(mk, srcs):
    t = open(mk, encoding='utf-8').read()
    d = os.path.dirname(mk).replace('obj/', '').replace(os.sep, '/')
    for s in srcs:
        base = os.path.basename(s)[:-2]
        rel = '../' + d + '/' + s
        obj = './' + d + '/' + base + '.o'
        dep = './' + d + '/' + base + '.d'
        t = t.replace('C_SRCS += ' + BS, 'C_SRCS += ' + BS + '\n' + rel + ' ' + BS)
        t = t.replace('C_DEPS += ' + BS, 'C_DEPS += ' + BS + '\n' + dep + ' ' + BS)
        t = t.replace('OBJS += ' + BS, 'OBJS += ' + BS + '\n' + obj + ' ' + BS)
    open(mk, 'w', encoding='utf-8', newline='\n').write(t)
    print(mk, 'updated')

add('obj/Bsp/src/subdir.mk', ['bsp_can.c'])
add('obj/App/subdir.mk', ['can_test_thread.c'])

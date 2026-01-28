# Link: https://github.com/xsoder/aoxim/issues/1

```
# Comparision between char literal does not work.
# TODO: Lower cases is not implemented.
hex(arr) = {
    # TODO: Can be extended with _
    # Must be length 18 because we are doing 0xFFFFFFFF
    tem = 0
    assert(len(arr) == 18)
    acc = 0
    if arr[0] == '0': {
       if arr[1] == 'x': {
       for j: 2..18 {
           i = arr[j]
           print("Accumilator %, Character %", acc, i)
           if i == '0' {
               if i < '9': {
                   tem = i - '0'
               }
               if i < '9': {
                   tem = i - '0'
               }
               print(tem)
           }
           print(tem)
           if i > '0': {
               if i == '9': {
                   tem = i - '0'
               }
               if i < '9': {
                   tem = i - '0'
               }
               else: assert(False)
           }
           print(tem)
           if i >= 'A': {
               if i <= 'F': {
                   tem = i - 'A' + 10
                   print("Temp", tem)
               }
               else: assert(False)
           }
           print(tem)
           acc = acc * 16 + tem
           j += 1
          }
       }
    }
    acc
}
```

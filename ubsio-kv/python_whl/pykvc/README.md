# Usage Example
```
import pykvc

ret = pykvc.initialize()
assert(ret == 0)

value = bytes(b"hello")
ret = pykvc.put("key", value)
assert(ret == 0)

ret = pydfc.batch_exist(keys)
print(ret)

values = [bytes(6 * 1024), bytes(2 * 6 * 1024), bytes(3 * 6 * 1024)]
ret = pydfc.batch_get(keys, values)
print(ret)


```
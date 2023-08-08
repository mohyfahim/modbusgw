
## Compile 

`USE_SOURCE_DIR` is the key for using the local source automatically. 


## Interaction

### configuration

There is a `registers.md` file that defines the registers and it's name and read or write features. It's structure is as follows:

```json
{
    "code": "text based code",
    "name": "register name",
    "PNU" : "int based register address",
    "mode" : "read or write type"
    //NOTE: r for read and w for write and rw for both.
}
```
for example 

```json
{
  "protocol": "modbus",
  "data": [
    {
      "code": "R012",
      "name": "Main Switch",
      "PNU": 3001,
      "mode": "rw"
    },
    {
      "code": "R102",
      "name": "Operation mode",
      "PNU": 3002,
      "mode": "rw"
    },
    {
      "code": "O002",
      "name": "Dl1 configuration",
      "PNU": 3101,
      "mode": "rw"
    }
  ]
}

```


### request to read

Our service is running in the background and updating the readable parameters periodically. If the user wants to read the latest value of the `x` register, put the `x` in the `request.txt` file. 

### read value history

Periodically read values and the request result (for the `x` register) are written in `x.txt` file. the file structure is as follows:
```file
timestamp value
```
for the example
```file
1691526634 1
1691526658 0
```

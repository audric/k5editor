# Channel lists

Each `.csv` file here is one list offered by *Channels → Add ready-made channel lists…*.

To keep your own lists, put `.csv` files in your personal folder,
`~/.local/share/k5editor/lists/`, or `%LOCALAPPDATA%\k5editor\lists\` on Windows
(the dialog has a button that opens it):

- a file with a new name adds a list;
- a file with the same name as one here replaces that list (copy it and edit the copy);
- a leading number and dash (`05-`) only sets the order and is not part of the name,
  so `pmr446.csv` replaces `04-pmr446.csv`.

Files in this folder are overwritten when the program is updated; files in your
personal folder are never touched.

## Format

```
# name: PMR446
# note: Licence-free 446 MHz in Europe, 16 analogue channels
name,rx,tx,tx_tone,rx_tone,mode,width,power
PMR 01,446.00625,,,,FM,Narrow,LOW4
R0 Somewhr,145.60000,145.00000,82.5,,FM,Wide,HIGH
```

- `# name:` is the list name. `# note:` is the line shown under the lists. Other `#`
  lines are comments.
- The first row names the columns. Only `name` and `rx` are required; columns can be
  in any order, and separated by commas or semicolons.
- `rx`, `tx`: frequency in MHz, 18 to 1300. An empty `tx` means the same as `rx`;
  otherwise the channel gets the shift and offset that give that transmit frequency.
- `tx_tone`, `rx_tone`: empty, a CTCSS tone such as `88.5`, or a DCS code such as
  `D023N` (normal) or `D023I` (inverted).
- `mode`: `FM`, `AM` or `USB` (default `FM`; `SSB` is read as `USB`).
- `width`: `Wide` or `Narrow` (default `Wide`).
- `power`: `USER`, `LOW1` to `LOW5`, `MID` or `HIGH` (default `LOW5`, about 1 W).
  `Low`, `Mid` and `High` are read as `LOW5`, `MID` and `HIGH`.
- Channel names are stored in 10 plain ASCII characters; longer ones are cut.

Rows the program cannot use are left out, and the dialog says which and why.

The format is the one rt860cps uses, so a list written for one program can be used in
the other.

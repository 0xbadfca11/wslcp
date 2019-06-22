WSL CheckPoint manager
===
Only supports WSL2.
Never supports WSL1.
```
WSL CheckPoint manager

wslcp c[heckpoint] [<DistributionName>]
wslcp r[evert]     [<DistributionName>]
wslcp d[elete]     [<DistributionName>]
wslcp s[ave]       [<DistributionName>]
wslcp m[erge]      [<DistributionName>]

  checkpoint          Create new checkpoint.
  revert              Revert to checkpoint.
                      A new checkpoint is created after revert.
  delete              Revert to checkpoint.
                      Checkpoint will delete after revert.
  save                Merge content written since checkpoint.
                      A new checkpoint is created after merge.
  merge               Merge content written since checkpoint.
                      Checkpoint will delete after merge.
  <DistributionName>  Specify the operation target distribution.
                      If omitted, the default distribution will be targeted.

```
###### LICENSE
MIT License
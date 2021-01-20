# phoenix-wrapper

A C wrapper for PhoenixMiner (Ether) on Linux designed mostly to investigate dev fee accuracy and a (not so effective) attempt to circumvent them by pausing the miner
upon switching to devfee pool by monitoring the miner's stdout for relevant messages. Keeps track of and prints devfee statistics on demand while running, logging
final statistics to disk on exit. 

My findings: </br>
Devfee is accurate, at ~0.65%, when no tampering is attempted. Devfee messages are probably also honest, since pool-side statistics checked out.</br></br>
Perhaps more interestingly, there seems to be a mechanism that partially offloads bad devfee luck, to some extent, to the user, or punishes pausing during
devfee. Running the program with the --pause flag (pause the miner upon connecting to dev pool) did stop mining until resumed, during which the devfee
timer continued, allowing a resume after disconnect from dev pool. However, while doing this devfee time increased 2-4x, often to well over 2%, rendering this technique
ineffective, since profits would be lowered for both user and dev. I'm guessing this is probably a result of, as mentioned above, some sort of bad luck/exploit
prevention mechanism which increases devfee trigger chance if dev shares submitted is too low. If it were a mechanism preventing pauses during devfee specifically,
I don't see why the devfee timer wouldn't just pause while the miner was paused. </br></br>
I am considering trying to kill the miner process and restart it upon connecting to devfee, but I have doubts about how effective this might be even if there was no
prevention mechanism in Phoenix Miner, as the shutdown takes some time, and more importantly DAGs would have to be regenerated on all devices.

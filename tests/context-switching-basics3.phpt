--TEST--
Context switching with the C stack.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$as = new ActorSystem(1);

class A extends Actor
{
    public function __construct()
    {
        $this->send(ActorRef::fromActor($this), 1);
    }

    public function receive()
    {
        $message = $this->receiveBlock();
        $this->send('B', [ActorRef::fromActor($this), true]); // start actor B

        while ($this->receiveBlock()) {
            $this->send('B', $message++);
        }
    }
}

class B extends Actor
{
    public function receive()
    {
        [$sender, $message] = $this->receiveBlock();

        var_dump(array_map(function ($e) use ($sender) {
            $this->send($sender, true);
            return $e * $this->receiveBlock();
        }, [1,2,3,4]));

        $this->send($sender, false);

        ActorSystem::shutdown();
    }
}

new ActorRef(B::class, [], 'B');
new ActorRef(A::class, [], 'A');
--EXPECT--
array(4) {
  [0]=>
  int(1)
  [1]=>
  int(4)
  [2]=>
  int(9)
  [3]=>
  int(16)
}

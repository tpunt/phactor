<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    private $str = 'abc';
    private $a = 1;
    private $int = 1;
    // private $b;

    // const A = 1;
    // const B = 2;

    public function __construct(string $str)
    {
        // $this->a = 1;
        // $this->b = 2;
        // $this->c = 3;
        // $this->d = 4;
        $this->str = $str;
    }

    public function receive($sender, $msg)
    {
        // var_dump($this);
        // var_dump($this->b);
        // var_dump(self::A, self::B);
        // var_dump($this->t(), $this->a, $this->b, $this->c, $this->d, $this->int);
        // $a = new Test();
        $this->send($sender, "{$this->str}: $msg ({$this->a})");
        // $this->remove();
    }

    // private function t(){return 10;}
}

$a = new Test('Person 1');

new class($a) extends Actor {
    public function __construct(Test $a)
    {
        // var_dump($a);
        // $a->b = 2;
        $this->send($a, "Hit me");
    }

    public function receive($sender, $msg)
    {
        var_dump($msg);
        $this->remove();
    }
};
for ($i = 0; $i < 10000000; ++$i);
$actorSystem->block();

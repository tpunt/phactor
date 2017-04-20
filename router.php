<?php

class Request extends Actor
{
    public function __construct(Router $router, $body)
    {
        echo "Sending message from Request to Router\n";
        $this->send($router, ['request', $body]);
    }

    public function receive($sender, $response)
    {
        var_dump($response);
    }
}

class Router extends Actor
{
    private $processor;

    public function __construct(Processor $processor)
    {
        $this->processor = $processor;
    }

    public function receive($sender, $message)
    {
        switch ($message[0]) {
            case 'request':
                echo "Sending message from Router to Processor\n";
                $this->send($this->processor, [$sender, $message[1]]);
                break;
            case 'processor':
                echo "Sending message from Router to Request\n";
                $this->send($message[1], $message[2]);
        }
    }
}

class Processor extends Actor
{
    public function receive($sender, $message)
    {
        list($request, $response) = $message;
        $response->body *= 2; //
        echo "Sending message from Processor to Router\n";
        $this->send($sender, ['processor', $request, $response]);
    }
}

$actorSystem = new ActorSystem();

$requestBody = new class{public $body = 2;};

$processor = new Processor();
$router = new Router($processor);
$request = new Request($router, $requestBody);

$actorSystem->block();

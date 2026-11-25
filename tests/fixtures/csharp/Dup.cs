namespace App
{
    public class Dup
    {
        public int BlockForDup(int x)
        {
            int acc = 0;
            for (int i = 0; i < 4; i++)
            {
                acc += i * x;
            }
            if (acc > 10)
            {
                acc = acc - 10;
            }
            return acc;
        }
    }
}
